#!/usr/bin/env python3
"""portal-spike — Stage 0 diagnostic (NOT product code; GNOME_PLAN.md D1-B/C).

Question it answers: can a NON-sandboxed Qt app really register a global
shortcut through org.freedesktop.portal.GlobalShortcuts on this GNOME — and
if the portal demands an identity, what exactly satisfies it?

    portal-spike.py check
    portal-spike.py list
    portal-spike.py bind ["<Ctrl><Alt>c"] [listen-seconds] [--app-id ID] [--desktop]

`bind` pops the portal's own dialog — that dialog is the answer to "will the
user understand this". Portal response codes: 0 ok, 1 cancelled, 2 failed.

--app-id makes the spike own a well-known bus name and export
org.freedesktop.Application.ApplicationId (what xdg-desktop-portal uses to
identify a non-sandboxed caller) and pass app_id in the options; --desktop
additionally drops a desktop file into ~/.local/share/applications, which
g_desktop_app_info_new() needs to resolve that id. The combinations tell us
what Traduku itself has to provide (SingleInstance name + installed
traduku.desktop — see D1-B/D5).
"""
import argparse
import os
import sys

from gi.repository import Gio, GLib

PORTAL = "org.freedesktop.portal.Desktop"
PATH = "/org/freedesktop/portal/desktop"
GS = "org.freedesktop.portal.GlobalShortcuts"
REQ = "org.freedesktop.portal.Request"
GNOME_IMPL = "org.freedesktop.impl.portal.desktop.gnome"

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
loop = GLib.MainLoop()

APPLICATION_XML = """
<node>
  <interface name="org.freedesktop.Application">
    <property name="ApplicationId" type="s" access="read"/>
    <property name="EnableMenus" type="b" access="read"/>
    <method name="Activate">
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
    <method name="Open">
      <arg type="as" name="uris" direction="in"/>
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
    <method name="ActivateAction">
      <arg type="s" name="action_name" direction="in"/>
      <arg type="av" name="parameter" direction="in"/>
      <arg type="a{sv}" name="platform_data" direction="in"/>
    </method>
  </interface>
</node>"""


def get_props(iface):
    out = {}
    for name in ("version", "SupportedMethods", "SupportedEvents", "SupportedOptions"):
        try:
            v = bus.call_sync(PORTAL, PATH, "org.freedesktop.DBus.Properties",
                              "Get", GLib.Variant("(ss)", (iface, name)),
                              GLib.VariantType("(v)"), Gio.DBusCallFlags.NONE,
                              3000, None)
            out[name] = v.unpack()[0]
        except GLib.Error as e:
            out[name] = f"<{e.message}>"
    return out


def introspect(name):
    try:
        return bus.call_sync(name, PATH, "org.freedesktop.DBus.Introspectable",
                             "Introspect", None, GLib.VariantType("(s)"),
                             Gio.DBusCallFlags.NONE, 3000, None).unpack()[0]
    except GLib.Error as e:
        return f"<{e.message}>"


def call(method, sig, params, timeout=10000):
    try:
        v = bus.call_sync(PORTAL, PATH, GS, method, GLib.Variant(sig, params),
                          None, Gio.DBusCallFlags.NONE, timeout, None)
    except GLib.Error as e:
        raise RuntimeError(f"{method}: {e}") from e
    return v.unpack()[0]


def wait_request(handle, seconds=60):
    """Run the main loop until this Request emits Response (or timeout)."""
    result = {}

    def cb(conn, sender, object_path, interface_name, signal_name, parameters, user_data):
        if object_path != handle:
            return
        response, results = parameters.unpack()
        result["response"] = response
        result["results"] = results
        loop.quit()

    sub = bus.signal_subscribe(None, REQ, "Response", handle, None, cb, None)
    GLib.timeout_add_seconds(seconds, loop.quit)
    loop.run()
    bus.signal_disconnect(sub)
    return result


def own_app_id(app_id):
    """Own a well-known name and export org.freedesktop.Application under it."""
    node = Gio.DBusNodeInfo.new_for_xml(APPLICATION_XML)
    iface = node.interfaces[0]
    path = "/" + app_id.replace(".", "/")

    def handle_call(conn, sender, object_path, interface_name, method_name,
                    parameters, invocation, user_data):
        invocation.return_value(None)

    def get_property(conn, sender, object_path, interface_name, property_name,
                     user_data):
        if property_name == "ApplicationId":
            return GLib.Variant("s", app_id)
        return GLib.Variant("b", False)

    bus.register_object(path, iface, handle_call, get_property, None)
    Gio.bus_own_name_on_connection(bus, app_id, Gio.BusNameOwnerFlags.NONE, None, None)
    print(f"  own name {app_id}, org.freedesktop.Application at {path}")


def install_desktop_file(app_id):
    apps = os.path.join(GLib.get_user_data_dir(), "applications")
    os.makedirs(apps, exist_ok=True)
    path = os.path.join(apps, f"{app_id}.desktop")
    with open(path, "w", encoding="utf-8") as f:
        f.write("[Desktop Entry]\n"
                "Type=Application\n"
                "Name=Traduku (spike)\n"
                "Exec=/bin/true\n"
                "Icon=accessories-dictionary\n"
                "Terminal=false\n"
                f"StartupWMClass={app_id}\n")
    print(f"  desktop file: {path}")
    return path


def create_session(app_id=None):
    options = {
        "handle_token": GLib.Variant("s", "tradukuspike"),
        "session_handle_token": GLib.Variant("s", "tradukuspike"),
    }
    if app_id:
        options["app_id"] = GLib.Variant("s", app_id)
    handle = call("CreateSession", "(a{sv})", (options,))
    r = wait_request(handle, 20)
    if r.get("response") != 0:
        raise RuntimeError(f"CreateSession response={r.get('response')} {r.get('results')}")
    session = r["results"].get("session_handle")
    if not session:
        raise RuntimeError(f"no session_handle in {r['results']}")
    print(f"  session = {session}")
    return session


def cmd_check(app_id, desktop):
    print(f"# {PORTAL} {PATH}")
    ifaces = {w for w in introspect(PORTAL).split('"')
              if w.startswith("org.freedesktop.portal.")}
    print(f"  interfaces advertised: {len(ifaces)}")
    print(f"  {GS}: {'yes' if GS in ifaces else 'NO'}")
    if GS not in ifaces:
        return 1
    for k, v in get_props(GS).items():
        print(f"  {k} = {v}")
    impl = introspect(GNOME_IMPL)
    print(f"  backend {GNOME_IMPL}: impl.GlobalShortcuts = "
          f"{'yes' if 'org.freedesktop.impl.portal.GlobalShortcuts' in impl else 'NO'}")

    rc = 0
    print("  --- identity ladder (which of these does the portal accept?) ---")
    # 1. bare caller: no name owned, no app_id
    try:
        create_session()
        print("  1) bare (no name, no app_id):            OK")
    except Exception as e:                                        # noqa: BLE001
        print(f"  1) bare (no name, no app_id):            FAIL {e}")
        rc = 1
    # 2. owning a well-known name + org.freedesktop.Application
    if app_id:
        own_app_id(app_id)
        try:
            create_session()
            print(f"  2) owns {app_id} + ApplicationId:      OK")
        except Exception as e:                                    # noqa: BLE001
            print(f"  2) owns {app_id} + ApplicationId:      FAIL {e}")
        # 3. + app_id passed in options
        try:
            create_session(app_id)
            print(f"  3) + app_id in options:                OK")
        except Exception as e:                                    # noqa: BLE001
            print(f"  3) + app_id in options:                FAIL {e}")
        # 4. + an installed desktop file for that id
        if desktop:
            install_desktop_file(app_id)
            try:
                create_session(app_id)
                print(f"  4) + {app_id}.desktop installed:       OK  <== recipe")
            except Exception as e:                                # noqa: BLE001
                print(f"  4) + {app_id}.desktop installed:       FAIL {e}")
    return rc


def cmd_list(app_id):
    session = create_session(app_id)
    handle = call("ListShortcuts", "(oa{sv})", (session, {}))
    r = wait_request(handle, 20)
    print(f"  ListShortcuts response={r.get('response')} "
          f"shortcuts={r.get('results', {}).get('shortcuts')}")
    return 0 if r.get("response") == 0 else 1


def cmd_bind(trigger, seconds, app_id):
    session = create_session(app_id)
    handle = call("ListShortcuts", "(oa{sv})", (session, {}))
    r = wait_request(handle, 20)
    print(f"  ListShortcuts response={r.get('response')} "
          f"shortcuts={r.get('results', {}).get('shortcuts')}")

    shortcuts = [("translate", {
        "description": GLib.Variant("s", "Translate the clipboard (Traduku)"),
        "preferred_trigger": GLib.Variant("s", trigger),
    })]
    handle = call("BindShortcuts", "(oa(sa{sv})sa{sv})", (session, shortcuts, "", {}))
    print(f"  BindShortcuts -> request {handle} (the portal dialog may appear now)")
    r = wait_request(handle, 90)
    print(f"  BindShortcuts response={r.get('response')} results={r.get('results')}")
    if r.get("response") != 0:
        return 1

    def cb(conn, sender, object_path, interface_name, signal_name, parameters, user_data):
        args = parameters.unpack()
        print(f"  {signal_name}: id={args[1]} timestamp={args[2]} options={args[3]}")

    bus.signal_subscribe(None, GS, "Activated", PATH, None, cb, None)
    bus.signal_subscribe(None, GS, "Deactivated", PATH, None, cb, None)
    print(f"  listening {seconds}s — press {trigger} twice now")
    GLib.timeout_add_seconds(seconds, loop.quit)
    loop.run()
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__,
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("mode", nargs="?", default="check",
                   choices=("check", "list", "bind"))
    p.add_argument("trigger", nargs="?", default="<Ctrl><Alt>c")
    p.add_argument("seconds", nargs="?", type=int, default=12)
    p.add_argument("--app-id", default=None,
                   help="own this bus name + export org.freedesktop.Application")
    p.add_argument("--desktop", action="store_true",
                   help="also install <app-id>.desktop into ~/.local/share/applications")
    a = p.parse_args()
    try:
        if a.mode == "check":
            return cmd_check(a.app_id, a.desktop)
        if a.mode == "list":
            return cmd_list(a.app_id)
        if a.app_id:
            own_app_id(a.app_id)
        return cmd_bind(a.trigger, a.seconds, a.app_id)
    except Exception as e:                                        # noqa: BLE001
        print(f"  ERROR {type(e).__name__}: {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
