#ifndef D3D8_LIGHT_SURVEY_H
#define D3D8_LIGHT_SURVEY_H

/* X2_LIGHT_SURVEY -- see d3d8_light_survey.c. */
#include "gpu_draw.h"

/* Called with the finished draw, once its lighting is filled in. */
void d3d8_light_survey(const GpuDraw *d);

/* What the run's draws asked of the lighting stage, printed at exit. */
void d3d8_light_survey_report(void);

#endif /* D3D8_LIGHT_SURVEY_H */
