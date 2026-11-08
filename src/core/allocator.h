// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

namespace bl {
/* Which allocator this build's new and malloc go to (round 419, ADR 0112): `libs` and the stress reports say it. */
const char *allocator_name();
/* True when oneTBB's proxy is built in and replaced the runtime's allocator (Windows asks the proxy itself). */
bool allocator_replaced();
}  // namespace bl
