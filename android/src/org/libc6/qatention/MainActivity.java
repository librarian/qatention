package org.libc6.qatention;

import android.os.Bundle;
import android.os.Build;
import android.view.View;
import android.view.WindowInsets;
import android.graphics.Color;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;
import java.security.KeyStore;
import java.nio.charset.StandardCharsets;
import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;
import org.qtproject.qt.android.bindings.QtActivity;

public class MainActivity extends QtActivity {
    public static volatile int insetTop, insetBottom, insetLeft, insetRight;
    private static volatile android.content.Context context;
    private static final String KEY = "qatention.passwords.v1";

    @Override public void onCreate(Bundle state) {
        context = getApplicationContext();
        super.onCreate(state);
        // Use one edge-to-edge geometry on all supported Android versions.
        // QML reserves these insets, including landscape cutouts and nav bars.
        if (Build.VERSION.SDK_INT >= 30) getWindow().setDecorFitsSystemWindows(false);
        else getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN |
            View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        getWindow().setStatusBarColor(Color.TRANSPARENT);
        getWindow().setNavigationBarColor(Color.TRANSPARENT);
        getWindow().getDecorView().setOnApplyWindowInsetsListener((view, insets) -> {
            if (Build.VERSION.SDK_INT >= 30) {
                android.graphics.Insets bars = insets.getInsets(
                    WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
                insetTop = bars.top; insetBottom = bars.bottom;
                insetLeft = bars.left; insetRight = bars.right;
            } else {
                insetTop = insets.getSystemWindowInsetTop();
                insetBottom = insets.getSystemWindowInsetBottom();
                insetLeft = insets.getSystemWindowInsetLeft();
                insetRight = insets.getSystemWindowInsetRight();
            }
            return view.onApplyWindowInsets(insets);
        });
        getWindow().getDecorView().requestApplyInsets();
    }

    private static SecretKey key() throws Exception {
        KeyStore store = KeyStore.getInstance("AndroidKeyStore");
        store.load(null);
        if (!store.containsAlias(KEY)) {
            KeyGenerator generator = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore");
            generator.init(new KeyGenParameterSpec.Builder(KEY,
                KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setUserAuthenticationRequired(false).build());
            generator.generateKey();
        }
        return (SecretKey) store.getKey(KEY, null);
    }

    public static synchronized boolean savePassword(String identity, String password) {
        try {
            android.content.SharedPreferences.Editor editor = context.getSharedPreferences("credentials", 0).edit();
            if (password.isEmpty()) return editor.remove(identity).commit();
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key());
            cipher.updateAAD(identity.getBytes(StandardCharsets.UTF_8));
            String value = Base64.encodeToString(cipher.getIV(), Base64.NO_WRAP) + ":" +
                Base64.encodeToString(cipher.doFinal(password.getBytes(StandardCharsets.UTF_8)), Base64.NO_WRAP);
            return editor.putString(identity, value).commit();
        } catch (Exception error) { return false; }
    }

    public static synchronized String loadPassword(String identity) {
        try {
            String value = context.getSharedPreferences("credentials", 0).getString(identity, "");
            if (value.isEmpty()) return "";
            String[] parts = value.split(":", 2);
            Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.DECRYPT_MODE, key(), new GCMParameterSpec(128, Base64.decode(parts[0], Base64.NO_WRAP)));
            cipher.updateAAD(identity.getBytes(StandardCharsets.UTF_8));
            return new String(cipher.doFinal(Base64.decode(parts[1], Base64.NO_WRAP)), StandardCharsets.UTF_8);
        } catch (Exception error) { return ""; }
    }
}
