#pragma once

/* X2_LIGHT_SURVEY -- see d3d8_light_survey.cpp. */
#include "gpu_draw.h"

namespace x2::d3d8 {

/* Called with the finished draw, once its lighting is filled in. */
void d3d8_light_survey(const GpuDraw *d);

/* What the run's draws asked of the lighting stage, printed at exit. */
void d3d8_light_survey_report(void);

} // namespace x2::d3d8
