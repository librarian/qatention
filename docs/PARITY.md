# ATENtion reference and validation

Reference: local ATENtion commit a99e97f500c8d7ca98dfb4bb836fc1dc3b86483d.
It includes features newer than published main e7acb90c1c8cab22a2a82ff22a0ef755f91816dd.

The Qt frontend follows MainWindow.xaml's eight menus and uses platform-default
widgets, native file dialogs, tabs and a dockable log. Session, input, storage,
codec and CLI behavior were compared with the corresponding original sources.

## Ported

| Area | Coverage |
| --- | --- |
| Connection | HTTPS/login/JNLP, manual tokens, TLS client identity override, certificate compatibility setting |
| Profiles | Named credential profiles, Windows DPAPI, atomic storage, restore open tab order and active tab |
| Sessions | Multiple tabs, background pausing/status polls, reconnect, explicit disconnect |
| Controls | Power commands, absolute mouse, input release, key macros, clipboard, custom combinations, hotplug |
| View | Fit/1:1, window-to-host size, fullscreen, smooth scaling, full-frame cadence, FPS/quality/chroma |
| Storage | Four-channel attach sequence, authenticated record, slot probe, bounded read-only SCSI, keepalive/detach |
| Video | Hermon, palette4/8, RLE type2/type4 full and incremental, per-session ASPEED |
| Diagnostics | User list, host status, session counters, log panel/file, packet capture, capture CLI |

## Deliberate differences and remaining limits

- Qt styling replaces WPF styling. Linux integration depends on available Qt platform themes.
- Relative/single mouse modes remain disabled, as in the reference application.
- Linux/macOS credentials use owner-readable JSON; Windows passwords use DPAPI.
- Profile identity/restoration currently uses names, rather than ATENtion's GUIDs.
- Session information reports average FPS and bytes since connection, rather than rolling rates.
- Rendering uses complete QImages and a single outstanding framebuffer request;
  the original's dirty-rectangle repaint optimization and deeper request pipeline are not reproduced.
- Clipboard typing assumes the remote US layout; local OS-reserved shortcuts may need menu macros.
- Media transfer and decoder work run on the Qt event loop with bounded allocations.
- With web authentication, CLI media mode first connects the console; a supplied JNLP
  can mount media directly without opening video.
- macOS cross-build previews require on-device validation; Android is not enabled.

## Validation boundary

Core tests cover fragmented console negotiation, strict certificate rejection,
fresh-token reconnect/cancellation, input release, codec vectors and bounds,
ISO reads and sense responses, media attachment/read/detach and occupied-slot refusal.
UI tests cover menu structure, tab creation/closure, fullscreen and text mapping.
Packaged Windows tests run under Wine on Linux, including DPAPI profile round trips.
The installed UI/TLS smoke test and CLI decoder check run without connecting to a BMC.

Physical Windows interaction, real BMC ISO booting, and firmware-specific SCSI
sequences still require hardware validation. Earlier live-video success does not
establish those new features.
