# The specs, and why the adaptors are generated

These two XML files are the interfaces a tray client must implement:

| file | canonical source |
|---|---|
| `org.kde.StatusNotifierItem.xml` | <https://invent.kde.org/frameworks/knotifications/-/blob/master/src/org.kde.StatusNotifierItem.xml> |
| `com.canonical.dbusmenu.xml` | the D-Bus Menu spec 0.4, as shipped by hosts, e.g. `/usr/share/gnome-shell/extensions/ubuntu-appindicators@ubuntu.com/interfaces-xml/DBusMenu.xml` |

Regenerate the headers (commit the headers, never the bodies — the bodies are
ours in `*-adaptor-impl.cpp`, so a changed signature breaks the build instead
of producing an interface no host can call):

```bash
cd tools/spike
/usr/lib/qt6/bin/qdbusxml2cpp --namespace lt -i snitypes.h -c SniAdaptor  -a sni-adaptor  xml/org.kde.StatusNotifierItem.xml
/usr/lib/qt6/bin/qdbusxml2cpp --namespace lt -i snitypes.h -c MenuAdaptor -a menu-adaptor xml/com.canonical.dbusmenu.xml
```

What this buys us, all of it measured on a live GNOME/Wayland session:

* `qdbusxml2cpp` maps neither `ai` nor `a(ia{sv})` — every one of them needs an
  explicit `org.qtproject.QtDBus.QtTypeName.InN`/`OutN` annotation, and the
  value has to be XML-escaped (`QList&lt;int&gt;`). Without the annotation the
  tool refuses the file, which is the good outcome.
* Hand-written signatures that looked right and were not:
  `GetGroupProperties` takes **two** in-args (`ai ids`, `as propertyNames`),
  and `Event`'s first argument is `i`, not `v`. Both failures are silent on
  the host side: the menu just never appears.
* Hosts also call methods that are not in the published spec
  (`ProvideXdgActivationToken`, `XAyatanaSecondaryActivate`); implement them as
  no-ops with a log line rather than leaving them unknown.
