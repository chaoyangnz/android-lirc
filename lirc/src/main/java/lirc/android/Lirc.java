package lirc.android;

import java.io.File;


public class Lirc {
    static {
        System.loadLibrary("android-lirc");
    }

    native int parse(String filename);

    native byte[] getInfraredBuffer(String irDevice, String irCode, int minBufSize);

    native String[] getDeviceList();

    native String[] getCommandList(String irDevice);

    Lirc() {
        File dir = new File("/data/data/android-lirc/log");
        dir.mkdirs();
    }

}
