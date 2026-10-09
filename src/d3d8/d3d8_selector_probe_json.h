#pragma once

#include "d3d8_selector_probe.h"

#include <cstdio>

namespace x2::d3d8 {

void d3d8_selector_probe_print_multiply_chain(
    FILE *output, const D3D8SelectorDrawEvidence *evidence);

} // namespace x2::d3d8
