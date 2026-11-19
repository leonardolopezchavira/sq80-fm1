// SPDX-License-Identifier: MIT
package io.github.leonardolopezchavira.sq80tablet;

import android.app.Activity;
import android.content.Intent;
import android.content.res.AssetManager;
import android.graphics.Color;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.HashMap;
import java.util.Map;

/**
 * The tablet page (assets/panel, from app/panel and the editor's engine, assets/edit) full screen. It is served from
 * https://sq80.app/ inside the app (a name of its own, so the page keeps its storage and may load its engine), never
 * from the network. USB MIDI comes from MidiBridge; files the page opens or saves go through Android's pickers.
 */
public final class MainActivity extends Activity implements MidiBridge.Page {
    static final String HOST = "sq80.app";
    private static final int PICK = 1, SAVE = 2;
    private WebView web;
    private MidiBridge midi;
    private ValueCallback<Uri[]> picked;
    private byte[] saving;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        web = new WebView(this);
        web.setBackgroundColor(Color.BLACK);
        setContentView(web);
        immersive();

        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setDatabaseEnabled(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setAllowFileAccess(false);
        s.setAllowContentAccess(true);
        s.setTextZoom(100);
        s.setSupportZoom(false);
        s.setLoadWithOverviewMode(true);
        s.setUseWideViewPort(true);
        if (Build.VERSION.SDK_INT >= 21) s.setMixedContentMode(WebSettings.MIXED_CONTENT_NEVER_ALLOW);

        midi = new MidiBridge(this, this);
        web.addJavascriptInterface(midi, "SQ80Android");
        web.setWebViewClient(new WebViewClient() {
            @Override
            public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest req) {
                Uri u = req.getUrl();
                if (!HOST.equals(u.getHost())) return new WebResourceResponse("text/plain", "utf-8", 404, "Not here", null, null);
                return asset(u.getPath());
            }
            @Override
            public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest req) {
                Uri u = req.getUrl();
                if (HOST.equals(u.getHost())) return false;
                try { startActivity(new Intent(Intent.ACTION_VIEW, u)); } catch (RuntimeException e) { /* (no browser) */ }
                return true;                           // other sites: in the browser, not here
            }
        });
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public boolean onShowFileChooser(WebView view, ValueCallback<Uri[]> cb, FileChooserParams p) {
                if (picked != null) picked.onReceiveValue(null);
                picked = cb;
                Intent i = new Intent(Intent.ACTION_GET_CONTENT);
                i.addCategory(Intent.CATEGORY_OPENABLE);
                i.setType("*/*");
                try { startActivityForResult(Intent.createChooser(i, "Open"), PICK); }
                catch (RuntimeException e) { picked = null; return false; }
                return true;
            }
        });
        web.loadUrl("https://" + HOST + "/panel/index.html");
    }

    private static final Map<String, String> TYPES = new HashMap<>();
    static {
        TYPES.put("html", "text/html"); TYPES.put("js", "text/javascript"); TYPES.put("css", "text/css");
        TYPES.put("wasm", "application/wasm"); TYPES.put("woff2", "font/woff2"); TYPES.put("json", "application/json");
        TYPES.put("webmanifest", "application/manifest+json"); TYPES.put("png", "image/png"); TYPES.put("svg", "image/svg+xml");
    }

    private WebResourceResponse asset(String path) {
        if (path == null || path.contains("..")) return null;
        String p = path.startsWith("/") ? path.substring(1) : path;
        if (p.isEmpty() || p.endsWith("/")) p += "index.html";
        String ext = p.lastIndexOf('.') >= 0 ? p.substring(p.lastIndexOf('.') + 1) : "";
        String type = TYPES.containsKey(ext) ? TYPES.get(ext) : "application/octet-stream";
        try {
            InputStream in = getAssets().open("site/" + p, AssetManager.ACCESS_STREAMING);
            Map<String, String> h = new HashMap<>();
            h.put("Cache-Control", "no-cache");
            h.put("Access-Control-Allow-Origin", "*");
            return new WebResourceResponse(type, type.startsWith("text") ? "utf-8" : null, 200, "OK", h, in);
        } catch (IOException e) {
            return new WebResourceResponse("text/plain", "utf-8", 404, "Not found", null, null);
        }
    }

    /* MidiBridge.Page */
    @Override
    public void js(final String script) {
        runOnUiThread(() -> { if (web != null) web.evaluateJavascript(script, null); });
    }

    @Override
    public void save(final String name, final byte[] data) {
        runOnUiThread(() -> {
            saving = data;
            Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType(name.endsWith(".json") ? "application/json" : name.endsWith(".mid") ? "audio/midi" : "application/octet-stream");
            i.putExtra(Intent.EXTRA_TITLE, name);
            try { startActivityForResult(i, SAVE); }
            catch (RuntimeException e) { Toast.makeText(this, "No way to save files here", Toast.LENGTH_LONG).show(); saving = null; }
        });
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        Uri u = res == RESULT_OK && data != null ? data.getData() : null;
        if (req == PICK) {
            if (picked != null) picked.onReceiveValue(u != null ? new Uri[] {u} : null);
            picked = null;
        } else if (req == SAVE) {
            byte[] b = saving;
            saving = null;
            if (u == null || b == null) return;
            try (OutputStream o = getContentResolver().openOutputStream(u)) {
                if (o == null) throw new IOException("no stream");
                o.write(b);
                Toast.makeText(this, "Saved", Toast.LENGTH_SHORT).show();
            } catch (IOException e) {
                Toast.makeText(this, "Not saved: " + e.getMessage(), Toast.LENGTH_LONG).show();
            }
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        if (midi != null) midi.changed(true);            // the FM-1 plugged in while the app was open
    }

    @Override
    public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (focus) immersive();
    }

    @Override
    protected void onPause() {
        super.onPause();
        js("document.dispatchEvent(new Event('visibilitychange'))");   // (the page keeps its project at once)
    }

    @Override
    public void onBackPressed() {
        js("window.__sq80back ? __sq80back() : 0");                   // (the back key: the page's, not out of the app)
    }

    private void immersive() {
        web.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
    }

    @Override
    protected void onDestroy() {
        if (web != null) { web.destroy(); web = null; }
        super.onDestroy();
    }
}
