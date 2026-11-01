#pragma once

// NSS exposes a member named "slots". Preserve Qt's keyword macro around the
// C headers, without changing Qt keyword handling in the rest of the project.
#pragma push_macro("slots")
#undef slots
#include <cert.h>
#include <nss.h>
#include <ocsp.h>
#pragma pop_macro("slots")
