
#include <Headers/kern_api.hpp>
#include <Headers/kern_patcher.hpp>
#include <IOKit/IOLib.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/pci/IOPCIDevice.h>
#include <kern/thread_call.h>
#include <pexpert/pexpert.h>
#include <sys/sysctl.h>

extern "C" void mp_rendezvous_no_intrs(void (*fn)(void *), void *arg);
extern "C" int cpu_number(void);

static inline uint64_t xpRdmsr(uint32_t m) { uint32_t lo, hi; asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(m)); return ((uint64_t)hi << 32) | lo; }
static inline void xpWrmsr(uint32_t m, uint64_t v) { asm volatile("wrmsr" : : "c"(m), "a"((uint32_t)v), "d"((uint32_t)(v >> 32))); }

static int gXpEet = 0, gXpCst = 0, gXpD3 = 0;   // status
static mach_vm_address_t gXpCstPtr = 0;
static bool gXpCstWant = true;   // live toggle debug.xepm.cst=0 keeps XCPM's idle table (coil whine from deep C-state transitions)

// ─── 1: energy-efficient turbo ───────────────────────────────────────────────────────────────────────────────────
static void xpEetOne(void *) {
	const uint64_t v = xpRdmsr(0x1FC);
	if (!(v & (1ULL << 19))) xpWrmsr(0x1FC, v | (1ULL << 19));
}

// ─── 2: XCPM idle table ──────────────────────────────────────────────────────────────────────────────────────────
// on = C8/C10 enabled, C3 disabled; !on = XCPM's own table back. Returns 1 (fix on), 0 (stock), 2-4 errors.
static int xpCstSet(bool on) {
	uint8_t *tab = gXpCstPtr ? *reinterpret_cast<uint8_t **>(gXpCstPtr) : nullptr;
	if (!tab) return 2;
	auto hint = [&](int i) { return *reinterpret_cast<volatile uint32_t *>(tab + i * 24 + 4); };
	auto en = [&](int i) { return reinterpret_cast<volatile uint32_t *>(tab + i * 24 + 8); };
	if (hint(0) != 0x60 || hint(2) != 0x40 || hint(6) != 0x10) return 3;
	const uint32_t want = on ? 1 : 0;
	if (*en(0) == want && *en(2) == want && *en(6) == !want) return on ? 1 : 0;
	if (MachInfo::setKernelWriting(true, KernelPatcher::kernelWriteLock) != KERN_SUCCESS) return 4;
	*en(0) = want; *en(2) = want; *en(6) = !want;
	MachInfo::setKernelWriting(false, KernelPatcher::kernelWriteLock);
	return on ? 1 : 0;
}
static int xpCstFix() { return xpCstSet(gXpCstWant); }

// ─── 3: D3hot for driverless functions ───────────────────────────────────────────────────────────────────────────
// Only PCI device IDs that actually appear as IOPCIDevice on TGL-LP are listed here.
// DPTF (0x9a03) was removed: on TGL the dynamic-tuning participants are ACPI objects (\_TZ_.FN00..,
// \_SB_.PC00.DPTF), not PCI functions, so IOService::serviceMatching("IOPCIDevice") never yields them.
// ISH 0x43fc was removed: that is the TGP-H (desktop) part; TGL-LP mobile uses 0xa0fc only.
static int xpD3() {
	static const uint16_t ids[] = {
		0x9a19,           // IPU camera (11th Gen Core Image Processing Unit)
		0x9a11,           // GNA (11th Gen Core Gauss Newton Algorithm device)
		0xa0fc,           // ISH (TGP-LP on-package)
		0x9a1b, 0x9a1d,   // Thunderbolt 4 / USB4 DMA #0/#1 (macOS has no host driver here)
	};
	int n = 0;
	OSIterator *it = IOService::getMatchingServices(IOService::serviceMatching("IOPCIDevice"));
	if (!it) return 0;
	while (auto *o = it->getNextObject()) {
		auto *d = OSDynamicCast(IOPCIDevice, o);
		if (!d) continue;
		if (IOService *cl = d->getClient())   // e.g. a USBToolBox instance on the TBT DMA that never matched
			if (cl->getState() & (kIOServiceMatchedState | kIOServiceRegisteredState)) continue;
		const uint32_t id = d->configRead32(0);
		if ((id & 0xffff) != 0x8086) continue;
		bool want = false;
		for (uint16_t w : ids) if (w == (id >> 16)) want = true;
		uint8_t pm = 0;
		if (!want || !d->findPCICapability(kIOPCIPowerManagementCapability, &pm) || !pm) continue;
		const uint16_t cs = d->configRead16(pm + 4);
		if ((cs & 3) != 3) { d->configWrite16(pm + 4, (uint16_t)(cs | 3)); n++; }
	}
	it->release();
	return n;
}

// ─── 4: MCHBAR PL1 ───────────────────────────────────────────────────────────────────────────────────────────────
static int gXpPl1 = 0, gXpPl1Set = 0;   // requested watts, written value (1/8 W units) read back
static void xpPl1() {
	if (gXpPl1 <= 0) return;
	uint64_t mch = 0;
	OSIterator *it = IOService::getMatchingServices(IOService::serviceMatching("IOPCIDevice"));
	if (!it) return;
	while (auto *o = it->getNextObject()) {
		auto *d = OSDynamicCast(IOPCIDevice, o);
		if (d && !d->getBusNumber() && !d->getDeviceNumber() && !d->getFunctionNumber()) {
			const uint64_t v = ((uint64_t)d->configRead32(0x4C) << 32) | d->configRead32(0x48);
			if (v & 1) mch = v & 0x7FFFFE0000ULL;
			break;
		}
	}
	it->release();
	if (!mch) return;
	const uint32_t unit = (uint32_t)(xpRdmsr(0x606) & 0xF);   // power unit 1/2^n W (3 -> 1/8 W)
	IOMemoryDescriptor *md = IOMemoryDescriptor::withPhysicalAddress(mch + 0x5000, 0x1000, kIODirectionInOut);
	if (!md) return;
	if (IOMemoryMap *mm = md->createMappingInTask(kernel_task, 0, kIOMapAnywhere | kIOMapInhibitCache)) {
		auto *lo = reinterpret_cast<volatile uint32_t *>(mm->getVirtualAddress() + 0x9A0);
		auto *hi = reinterpret_cast<volatile uint32_t *>(mm->getVirtualAddress() + 0x9A4);
		if (!(*hi & 0x80000000U)) {   // lock bit 63
			const uint32_t want = (uint32_t)gXpPl1 << unit;
			if (want && want <= 0x7FFF && (*lo & 0x7FFF) != want) *lo = (*lo & ~0x7FFFU) | want | 0x8000U;   // + enable
			gXpPl1Set = (int)(*lo & 0x7FFF);
		} else gXpPl1Set = -1;
		mm->release();
	}
	md->release();
}

static void xpApply(thread_call_param_t, thread_call_param_t) {
	xpPl1();
	gXpCst = xpCstFix();
	gXpD3 += xpD3();
	if (!gXpEet) { mp_rendezvous_no_intrs(xpEetOne, nullptr); gXpEet = (int)((xpRdmsr(0x1FC) >> 19) & 1); }
	IOLog("XeNG PM (TGL): profile on: EE turbo off %d, C8/C10 %d, D3 %d, PL1 %d W (0x%x)\n",
	      gXpEet, gXpCst, gXpD3, gXpPl1, gXpPl1Set);
}

// ─── sensors (always registered, read only): debug.xepm.sensors ──────────────────────────────────────────────────
// The stack has no working CPU temperature source (SMCProcessor finds no instances), so the menu-bar tool reads this:
// struct { u32 version 1, ncpu, tjmax, pkg_temp_c; u8 core_temp_c[32]; u8 core_type[32] (0x40 P, 0x20 E; always 0 on
//          TGL); u32 energy_unit_bits; u32 pad; u64 pkg_energy (0x611), ia_energy (0x639), gt_energy (0x641) }.
// Temperatures are TjMax minus the digital readout of IA32_THERM_STATUS / IA32_PACKAGE_THERM_STATUS.
struct XpSensors { uint32_t ver, ncpu, tjmax, pkg; uint8_t core[32], type[32]; uint32_t eunit, pad; uint64_t epkg, eia, egt; };
static XpSensors gXpSens;
static void xpSensOne(void *) {
	const int c = cpu_number();
	if (c < 0 || c >= 32) return;
	const uint32_t tj = (uint32_t)(xpRdmsr(0x1A2) >> 16) & 0xff, dro = (uint32_t)(xpRdmsr(0x19C) >> 16) & 0x7f;
	gXpSens.core[c] = (uint8_t)(tj > dro ? tj - dro : 0);
	gXpSens.type[c] = 0;   // CPUID leaf 1AH core-type is hybrid-only, not enumerable on TGL (field kept for layout)
	__sync_fetch_and_add(&gXpSens.ncpu, 1);
}
static int xpSensors SYSCTL_HANDLER_ARGS {
	bzero(&gXpSens, sizeof(gXpSens));
	gXpSens.ver = 1;
	mp_rendezvous_no_intrs(xpSensOne, nullptr);
	gXpSens.tjmax = (uint32_t)(xpRdmsr(0x1A2) >> 16) & 0xff;
	const uint32_t pd = (uint32_t)(xpRdmsr(0x1B1) >> 16) & 0x7f;
	gXpSens.pkg = gXpSens.tjmax > pd ? gXpSens.tjmax - pd : 0;
	gXpSens.eunit = (uint32_t)(xpRdmsr(0x606) >> 8) & 0x1f;
	gXpSens.epkg = xpRdmsr(0x611) & 0xffffffffULL;
	gXpSens.eia = xpRdmsr(0x639) & 0xffffffffULL;
	gXpSens.egt = xpRdmsr(0x641) & 0xffffffffULL;
	return SYSCTL_OUT(req, &gXpSens, sizeof(gXpSens));
}

SYSCTL_NODE(_debug, OID_AUTO, xepm, CTLFLAG_RW, 0, "NootedGreen XE-PM");
SYSCTL_PROC(_debug_xepm, OID_AUTO, sensors, CTLTYPE_OPAQUE | CTLFLAG_RD | CTLFLAG_ANYBODY, nullptr, 0, xpSensors, "S", "temperatures and RAPL energy");
SYSCTL_INT(_debug_xepm, OID_AUTO, eet_off, CTLFLAG_RD, &gXpEet, 0, "");
static int xpCstCtl SYSCTL_HANDLER_ARGS {   // sudo sysctl -w debug.xepm.cst=0|1 (live)
	int v = gXpCst;
	int err = SYSCTL_OUT(req, &v, sizeof(v));
	if (err || !req->newptr) return err;
	if ((err = SYSCTL_IN(req, &v, sizeof(v)))) return err;
	gXpCstWant = v != 0;
	gXpCst = xpCstSet(gXpCstWant);
	return gXpCst >= 2 ? EIO : 0;
}
SYSCTL_PROC(_debug_xepm, OID_AUTO, cst, CTLTYPE_INT | CTLFLAG_RW, nullptr, 0, xpCstCtl, "I", "1 = C8/C10 on (write 0/1, root)");
SYSCTL_INT(_debug_xepm, OID_AUTO, d3, CTLFLAG_RD, &gXpD3, 0, "");
SYSCTL_INT(_debug_xepm, OID_AUTO, pl1_w, CTLFLAG_RD, &gXpPl1, 0, "requested PL1 (ngreenpmpl1)");
SYSCTL_INT(_debug_xepm, OID_AUTO, pl1_set, CTLFLAG_RD, &gXpPl1Set, 0, "MCHBAR PL1 field after the write, -1 = locked");

void xePmInstall(KernelPatcher &patcher) {
	sysctl_register_oid(&sysctl__debug_xepm);           // the sensors are always there (menu-bar temperature tool)
	sysctl_register_oid(&sysctl__debug_xepm_sensors);

	int w = 0;
	if (PE_parse_boot_argn("ngreenpmpl1", &w, sizeof(w)) && w >= 5 && w <= 64) gXpPl1 = w;
	gXpCstPtr = patcher.solveSymbol(KernelPatcher::KernelID, "_xcpm_cstate_table");
	patcher.clearError();
	sysctl_register_oid(&sysctl__debug_xepm_eet_off);
	sysctl_register_oid(&sysctl__debug_xepm_cst);
	sysctl_register_oid(&sysctl__debug_xepm_d3);
	sysctl_register_oid(&sysctl__debug_xepm_pl1_w);
	sysctl_register_oid(&sysctl__debug_xepm_pl1_set);
	uint64_t dl;
	thread_call_t t20 = thread_call_allocate(xpApply, nullptr), t60 = thread_call_allocate(xpApply, nullptr);
	clock_interval_to_deadline(20, kSecondScale, &dl);
	if (t20) thread_call_enter_delayed(t20, dl);
	clock_interval_to_deadline(60, kSecondScale, &dl);
	if (t60) thread_call_enter_delayed(t60, dl);
	IOLog("XeNG PM (TGL): -ngreenpm, idle table %s; profile in 20 s\n", gXpCstPtr ? "found" : "missing");
}
