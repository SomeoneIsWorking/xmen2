package com.someoneisworking.xmen2;

import io.github.someoneisworking.lucent.LucentActivity;

/** SDL's lifecycle owner. Install acquisition never starts this Activity early. */
public final class XMen2GameActivity extends LucentActivity {
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
        return new String[]{"--appimage"};
    }

}
