// SPDX-License-Identifier: MIT
package io.github.leonardolopezchavira.sq80tablet;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.pm.PackageManager;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.media.midi.MidiDevice;
import android.media.midi.MidiDeviceInfo;
import android.media.midi.MidiInputPort;
import android.media.midi.MidiManager;
import android.media.midi.MidiOutputPort;
import android.media.midi.MidiReceiver;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.HandlerThread;
import android.webkit.JavascriptInterface;

import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.PriorityBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.LockSupport;

/**
 * USB MIDI for the page (app.js's bridge: window.SQ80Android, calling back __sq80midi and __sq80ports). Android's
 * MIDI service when the device has it; else (some Fire OS builds) the USB MIDI class straight off the USB host API.
 * Sends may be timestamped ahead (a delay in ms): a thread of their own sends each when it is due.
 */
public final class MidiBridge {
    public interface Page {
        void js(String script);                 // run in the page (the UI thread's business)
        void save(String name, byte[] data);
    }

    private static final String ACTION_PERMISSION = "io.github.leonardolopezchavira.sq80tablet.USB_PERMISSION";
    private final Context ctx;
    private final Page page;
    private final MidiManager mm;
    private final UsbManager usb;
    private final Handler bg;
    private final Map<String, Port> ports = new HashMap<>();
    private final PriorityBlockingQueue<Out> queue = new PriorityBlockingQueue<>();
    private final AtomicLong seq = new AtomicLong();
    private final StringBuilder pending = new StringBuilder();
    private boolean flushPosted;

    /** a device: what it sends us (in), where we send (out) */
    private abstract static class Port {
        String id, name;
        abstract boolean open();
        abstract void write(byte[] b) throws IOException;
        abstract void close();
    }

    private static final class Out implements Comparable<Out> {
        final long due, n;
        final Port port;
        final byte[] data;
        Out(long due, long n, Port port, byte[] data) { this.due = due; this.n = n; this.port = port; this.data = data; }
        @Override public int compareTo(Out o) { return due != o.due ? Long.compare(due, o.due) : Long.compare(n, o.n); }
    }

    public MidiBridge(Context ctx, Page page) {
        this.ctx = ctx;
        this.page = page;
        HandlerThread t = new HandlerThread("sq80-midi");
        t.start();
        bg = new Handler(t.getLooper());
        MidiManager m = null;
        if (ctx.getPackageManager().hasSystemFeature(PackageManager.FEATURE_MIDI))
            m = (MidiManager) ctx.getSystemService(Context.MIDI_SERVICE);
        mm = m;
        usb = (UsbManager) ctx.getSystemService(Context.USB_SERVICE);
        if (mm != null) {
            mm.registerDeviceCallback(new MidiManager.DeviceCallback() {
                @Override public void onDeviceAdded(MidiDeviceInfo info) { changed(true); }
                @Override public void onDeviceRemoved(MidiDeviceInfo info) { changed(false); }
            }, bg);
        }
        IntentFilter f = new IntentFilter(ACTION_PERMISSION);
        f.addAction(UsbManager.ACTION_USB_DEVICE_ATTACHED);
        f.addAction(UsbManager.ACTION_USB_DEVICE_DETACHED);
        ctx.registerReceiver(new BroadcastReceiver() {
            @Override public void onReceive(Context c, Intent i) {
                boolean in = !UsbManager.ACTION_USB_DEVICE_DETACHED.equals(i.getAction());
                bg.postDelayed(() -> changed(in), in ? 300 : 0);
            }
        }, f);
        Thread s = new Thread(this::sender, "sq80-midi-out");
        s.setPriority(Thread.MAX_PRIORITY);
        s.setDaemon(true);
        s.start();
    }

    /** a device came or went (or a permission was granted): the page looks again */
    public void changed(boolean attached) {
        page.js("window.__sq80ports && __sq80ports();" + (attached ? "window.__sq80attached && __sq80attached();" : ""));
    }

    /* ----------------------------------------------------------------------------------------- the page's side */
    @JavascriptInterface
    public String ports() {
        synchronized (ports) {
            List<Port> now = list();
            Map<String, Port> keep = new HashMap<>();
            for (Port p : now) keep.put(p.id, ports.containsKey(p.id) ? ports.get(p.id) : p);
            for (Map.Entry<String, Port> e : ports.entrySet())
                if (!keep.containsKey(e.getKey())) e.getValue().close();
            ports.clear();
            ports.putAll(keep);
            StringBuilder in = new StringBuilder(), out = new StringBuilder();
            for (Port p : ports.values()) {
                String j = "{\"id\":\"" + p.id + "\",\"name\":\"" + p.name.replace("\\", "").replace("\"", "'") + "\"}";
                in.append(in.length() > 0 ? "," : "").append(j);
                out.append(out.length() > 0 ? "," : "").append(j);
            }
            return "{\"inputs\":[" + in + "],\"outputs\":[" + out + "]}";
        }
    }

    @JavascriptInterface
    public void openInput(String id) { open(id); }

    @JavascriptInterface
    public void openOutput(String id) { open(id); }

    private void open(String id) {
        Port p;
        synchronized (ports) { p = ports.get(id); }
        if (p != null) p.open();
    }

    @JavascriptInterface
    public void send(String id, String hex, double delayMs) {
        Port p;
        synchronized (ports) { p = ports.get(id); }
        if (p == null || hex == null) return;
        byte[] b = new byte[hex.length() / 2];
        for (int i = 0; i < b.length; i++) b[i] = (byte) Integer.parseInt(hex.substring(2 * i, 2 * i + 2), 16);
        long due = System.nanoTime() + (long) (Math.max(0, delayMs) * 1e6);
        queue.add(new Out(due, seq.incrementAndGet(), p, b));
    }

    @JavascriptInterface
    public void save(String name, String base64) {
        page.save(name, android.util.Base64.decode(base64, android.util.Base64.DEFAULT));
    }

    private void sender() {
        while (true) {
            try {
                Out o = queue.take();
                long wait = o.due - System.nanoTime();
                if (wait > 200_000) {                 // not yet: back in, and sleep until it (or an earlier one) is due
                    queue.add(o);
                    LockSupport.parkNanos(Math.min(wait, 2_000_000));
                    continue;
                }
                try { o.port.write(o.data); } catch (IOException | RuntimeException e) { /* (gone) */ }
            } catch (InterruptedException e) {
                return;
            }
        }
    }

    /* what came in, to the page: whole messages (the clock and other real time left out), a few at a time */
    private final Runnable flush = new Runnable() {
        @Override public void run() {
            String s;
            synchronized (pending) { s = pending.toString(); pending.setLength(0); flushPosted = false; }
            if (s.length() > 0) page.js(s);
        }
    };

    private void deliver(String id, byte[] m, int n) {
        if (n <= 0 || (m[0] & 0xFF) >= 0xF8) return;
        StringBuilder h = new StringBuilder(n * 2);
        for (int i = 0; i < n; i++) { int v = m[i] & 0xFF; h.append(Character.forDigit(v >> 4, 16)).append(Character.forDigit(v & 15, 16)); }
        synchronized (pending) {
            pending.append("window.__sq80midi&&__sq80midi('").append(id).append("','").append(h).append("');");
            if (!flushPosted) { flushPosted = true; bg.postDelayed(flush, 2); }
        }
    }

    /** whole messages from a byte stream: running status, SysEx across pieces, real time in between */
    private final class Parser {
        final String id;
        final byte[] buf = new byte[8192];
        int n, need, running;
        boolean sysex;
        Parser(String id) { this.id = id; }
        void feed(byte[] d, int off, int count) {
            for (int i = off; i < off + count; i++) {
                int b = d[i] & 0xFF;
                if (b >= 0xF8) { continue; }
                if (b == 0xF0) { sysex = true; n = 0; buf[n++] = (byte) b; continue; }
                if (sysex) {
                    if (b == 0xF7) { if (n < buf.length) buf[n++] = (byte) b; deliver(id, buf, n); sysex = false; n = 0; continue; }
                    if (b < 0x80) { if (n < buf.length) buf[n++] = (byte) b; continue; }
                    sysex = false; n = 0;            // (a status byte ends it, unfinished)
                }
                if (b >= 0x80) {
                    n = 0; buf[n++] = (byte) b;
                    int hi = b & 0xF0;
                    need = hi == 0xC0 || hi == 0xD0 || b == 0xF1 || b == 0xF3 ? 2 : b == 0xF6 || b == 0xF7 ? 1 : 3;
                    running = b < 0xF0 ? b : 0;
                    if (need == 1) { deliver(id, buf, 1); n = 0; }
                    continue;
                }
                if (n == 0) {                        // running status
                    if (running == 0) continue;
                    buf[n++] = (byte) running;
                    int hi = running & 0xF0;
                    need = hi == 0xC0 || hi == 0xD0 ? 2 : 3;
                }
                buf[n++] = (byte) b;
                if (n == need) { deliver(id, buf, n); n = 0; }
            }
        }
    }

    /* ------------------------------------------------------------------------------------- the ports there are */
    private List<Port> list() {
        List<Port> r = new ArrayList<>();
        if (mm != null) {
            for (MidiDeviceInfo info : mm.getDevices()) {
                if (info.getInputPortCount() < 1 || info.getOutputPortCount() < 1) continue;
                r.add(new ServicePort(info));
            }
        } else if (usb != null) {
            for (UsbDevice d : usb.getDeviceList().values()) {
                UsbPort p = UsbPort.of(this, d);
                if (p != null) r.add(p);
            }
        }
        return r;
    }

    /* Android's MIDI service */
    private final class ServicePort extends Port {
        final MidiDeviceInfo info;
        MidiDevice dev;
        MidiInputPort to;
        MidiOutputPort from;
        ServicePort(MidiDeviceInfo info) {
            this.info = info;
            id = "m" + info.getId();
            Bundle p = info.getProperties();
            String n = p.getString(MidiDeviceInfo.PROPERTY_PRODUCT);
            if (n == null) n = p.getString(MidiDeviceInfo.PROPERTY_NAME);
            name = n != null ? n : "MIDI " + info.getId();
        }
        @Override synchronized boolean open() {
            if (dev != null) return true;
            final CountDownLatch done = new CountDownLatch(1);
            final MidiDevice[] got = new MidiDevice[1];
            mm.openDevice(info, d -> { got[0] = d; done.countDown(); }, bg);
            try { done.await(3, TimeUnit.SECONDS); } catch (InterruptedException e) { return false; }
            if (got[0] == null) return false;
            dev = got[0];
            to = dev.openInputPort(0);
            from = dev.openOutputPort(0);
            if (from != null) {
                final Parser parser = new Parser(id);
                from.connect(new MidiReceiver() {
                    @Override public void onSend(byte[] msg, int offset, int count, long timestamp) { parser.feed(msg, offset, count); }
                });
            }
            return to != null;
        }
        @Override void write(byte[] b) throws IOException { if (to != null) to.send(b, 0, b.length); }
        @Override synchronized void close() {
            try { if (to != null) to.close(); if (from != null) from.close(); if (dev != null) dev.close(); } catch (IOException e) { /* (gone) */ }
            dev = null; to = null; from = null;
        }
    }

    /* the USB MIDI class, without the MIDI service: 4-byte event packets on two bulk endpoints */
    private static final class UsbPort extends Port {
        final MidiBridge br;
        final UsbDevice dev;
        final UsbInterface itf;
        final UsbEndpoint epIn, epOut;
        UsbDeviceConnection conn;
        Thread reader;
        volatile boolean running;

        static UsbPort of(MidiBridge br, UsbDevice d) {
            for (int i = 0; i < d.getInterfaceCount(); i++) {
                UsbInterface f = d.getInterface(i);
                if (f.getInterfaceClass() != UsbConstants.USB_CLASS_AUDIO || f.getInterfaceSubclass() != 3) continue;
                UsbEndpoint in = null, out = null;
                for (int e = 0; e < f.getEndpointCount(); e++) {
                    UsbEndpoint ep = f.getEndpoint(e);
                    if (ep.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;
                    if (ep.getDirection() == UsbConstants.USB_DIR_IN) { if (in == null) in = ep; } else if (out == null) out = ep;
                }
                if (in != null && out != null) return new UsbPort(br, d, f, in, out);
            }
            return null;
        }
        UsbPort(MidiBridge br, UsbDevice d, UsbInterface f, UsbEndpoint in, UsbEndpoint out) {
            this.br = br; dev = d; itf = f; epIn = in; epOut = out;
            id = "u" + d.getDeviceId();
            String n = Build.VERSION.SDK_INT >= 21 ? d.getProductName() : null;
            name = n != null ? n : "USB MIDI " + d.getDeviceId();
        }
        @Override synchronized boolean open() {
            if (conn != null) return true;
            if (!br.usb.hasPermission(dev)) {          // asked once; the answer comes back as changed()
                int fl = Build.VERSION.SDK_INT >= 31 ? 0x02000000 /* FLAG_MUTABLE */ : 0;
                Intent i = new Intent(ACTION_PERMISSION);
                i.setPackage(br.ctx.getPackageName());
                br.usb.requestPermission(dev, PendingIntent.getBroadcast(br.ctx, 0, i, fl));
                return false;
            }
            conn = br.usb.openDevice(dev);
            if (conn == null) return false;
            if (!conn.claimInterface(itf, true)) { conn.close(); conn = null; return false; }
            running = true;
            final Parser parser = br.new Parser(id);
            reader = new Thread(() -> {
                byte[] b = new byte[Math.max(64, epIn.getMaxPacketSize())];
                byte[] m = new byte[3];
                while (running) {
                    int n = conn.bulkTransfer(epIn, b, b.length, 250);
                    for (int i = 0; i + 3 < n; i += 4) {
                        int cin = b[i] & 15, len = CIN_LEN[cin];
                        if (len == 0) continue;
                        System.arraycopy(b, i + 1, m, 0, 3);
                        parser.feed(m, 0, len);
                    }
                }
            }, "sq80-usb-in");
            reader.setDaemon(true);
            reader.start();
            return true;
        }
        static final int[] CIN_LEN = {0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1};
        @Override void write(byte[] b) throws IOException {
            UsbDeviceConnection c = conn;
            if (c == null) throw new IOException("not open");
            byte[] p = packets(b);
            int max = Math.max(4, epOut.getMaxPacketSize() / 4 * 4);
            for (int o = 0; o < p.length; o += max) {
                int n = Math.min(max, p.length - o);
                byte[] chunk = new byte[n];
                System.arraycopy(p, o, chunk, 0, n);
                if (c.bulkTransfer(epOut, chunk, n, 250) < 0) throw new IOException("USB write failed");
            }
        }
        /** one MIDI message (or a whole SysEx) as USB MIDI event packets, cable 0 */
        static byte[] packets(byte[] b) {
            int s = b[0] & 0xFF;
            if (s == 0xF0) {
                int n = (b.length + 2) / 3;
                byte[] p = new byte[n * 4];
                for (int k = 0, i = 0; i < b.length; k++, i += 3) {
                    int left = b.length - i;
                    int cin = left > 3 ? 4 : left == 3 ? 7 : left == 2 ? 6 : 5;
                    if (left <= 3 && (b[b.length - 1] & 0xFF) != 0xF7) cin = 4;
                    p[4 * k] = (byte) cin;
                    for (int j = 0; j < 3 && i + j < b.length; j++) p[4 * k + 1 + j] = b[i + j];
                }
                return p;
            }
            int cin = s >= 0xF8 ? 0xF : s < 0xF0 ? s >> 4 : s == 0xF2 ? 3 : s == 0xF1 || s == 0xF3 ? 2 : 5;
            byte[] p = new byte[4];
            p[0] = (byte) cin;
            for (int j = 0; j < 3 && j < b.length; j++) p[1 + j] = b[j];
            return p;
        }
        @Override synchronized void close() {
            running = false;
            if (conn != null) { try { conn.releaseInterface(itf); } catch (RuntimeException e) { /* (gone) */ } conn.close(); conn = null; }
        }
    }
}
