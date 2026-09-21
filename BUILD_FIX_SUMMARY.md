# Build Failure Analysis and Fix

## Problem
The cppload-workflow C++ project was failing to compile after hardening changes were made. The build failed with exit code 1, and error messages were not visible in the initial output.

## Root Cause
The file `core/net/connection.cpp` was using the function `host_is_ip_literal()` at line 255 but was missing the required include for `cppload/net/utils.hpp` where this function is declared.

### Error Message (captured via direct compilation)
```
D:/cppload-workflow/core/net/connection.cpp:255:26: error: 'host_is_ip_literal' was not declared in this scope
  255 |                     if (!host_is_ip_literal(host) && !SSL_set_tlsext_host_name(
      |                          ^~~~~~~~~~~~~~~~~~
```

## Why Error Messages Were Not Visible
When running `ninja` build, the error output was being captured but the build system was stopping at the first failure. The error messages were present but needed to be extracted from the full build log. Direct compilation of individual files with `g++` made the errors immediately visible.

## Fix Applied
Added the missing include to `core/net/connection.cpp`:

```cpp
// @author ssrjkk | cppload
#include "cppload/net/connection.hpp"
#include "cppload/net/utils.hpp"  // <-- ADDED THIS LINE
#include <boost/beast/core.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/asio/write.hpp>
#include <memory>
```

## Verification
After the fix:
- All 10 originally failing files now compile successfully
- Verified across all three build configurations:
  - `build/` (Release)
  - `build-debug/` (Debug with -g0 -O0)
  - `build-shared/` (Release with -O3)
- All files using `host_is_ip_literal()` now have the proper include:
  - `core/net/connection.cpp` - FIXED
  - `core/net/http_client.cpp` - Already had include
  - `core/net/tcp_raw_client.cpp` - Already had include
  - `core/net/ws_client.cpp` - Already had include

## Additional Warnings Found (Non-blocking)
Two warnings were found during compilation but do not prevent building:

1. `core/net/http_client.cpp:219` - Unused variable 'n' in `cached_connection_dead()`
2. `core/scenario/engine.cpp:409` - Variable 't_sleep0' set but not used

These are warnings only and do not cause build failures unless `-Werror` is enabled.

## Files Modified
- `D:/cppload-workflow/core/net/connection.cpp` - Added missing include for `cppload/net/utils.hpp`
