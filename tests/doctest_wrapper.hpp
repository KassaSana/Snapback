// The one include site for doctest — every test includes this instead of
// <doctest/doctest.h> directly (Roadmap 6.5).
//
// Why: doctest v2.4.11 specializes std::tuple, which MSVC flags as C5285 once
// per translation unit. 24 TUs × 1 warning buried our *own* diagnostics — part
// of why the 6.1 stack overflow took a crash to surface rather than a warning
// someone read. Third-party noise gets silenced here, at the boundary, so the
// project's warning output stays meaningful. Do not blanket-disable C5285 for
// our own code; specializing std library templates really is forbidden.
#pragma once

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 5285)  // "cannot declare a specialization for 'std::tuple'"
#endif

#include <doctest/doctest.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
