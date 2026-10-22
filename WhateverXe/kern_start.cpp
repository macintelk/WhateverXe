//
//  kern_start.cpp
//  WhateverXe
//
//  Copyright © 2017 vit9696. All rights reserved.
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

#include "kern_host.hpp"

static XeHost host;

static const char *bootargOff[] {
	"-wexoff"
};

static const char *bootargDebug[] {
	"-wexdbg"
};

static const char *bootargBeta[] {
	"-wexbeta"
};

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal | LiluAPI::AllowInstallerRecovery | LiluAPI::AllowSafeMode,
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::BigSur,
	KernelVersion::Tahoe,
	[]() {
		host.init();
	}
};

