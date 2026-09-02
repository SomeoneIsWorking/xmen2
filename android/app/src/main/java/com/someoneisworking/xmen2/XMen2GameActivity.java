package com.someoneisworking.xmen2;

import io.github.someoneisworking.lucent.LucentActivity;

/** SDL's lifecycle owner. Install acquisition never starts this Activity early. */
public final class XMen2GameActivity extends LucentActivity {
    static final String GPU_SELFTEST = "com.someoneisworking.xmen2.debug.gpu_selftest";

    @Override
    protected String[] getLibraries() {
        return new String[]{"SDL3", "main"};
    }

    @Override
    protected String getMainFunction() {
        return "main";
    }

    @Override
    protected String[] getArguments() {
        if (BuildConfig.DEBUG && getIntent().getBooleanExtra(GPU_SELFTEST, false)) {
            return new String[]{"--appimage", "--vk-selftest"};
        }
        return new String[]{"--appimage"};
    }

}
