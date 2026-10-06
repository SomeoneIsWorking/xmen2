#include "d3d8_selector_probe_json.h"

void d3d8_selector_probe_print_multiply_chain(
    FILE *output, const D3D8SelectorDrawEvidence *evidence) {
  uint32_t step_index, value_index;
  fputs(",\"world_matrix_multiply_chain\":[", output);
  for (step_index = 0; step_index < evidence->world_matrix_multiply_chain_count;
       ++step_index) {
    const D3D8SelectorMultiplyStep *step =
        &evidence->world_matrix_multiply_chain[step_index];
    fprintf(output,
            "%s{\"output\":\"%08x\",\"caller\":\"%08x\","
            "\"left\":\"%08x\",\"right\":\"%08x\","
            "\"inputs_readable\":%s,"
            "\"left_copy_found\":%s,"
            "\"left_copy_caller\":\"%08x\","
            "\"left_copy_source\":\"%08x\","
            "\"left_copy_source_readable\":%s,"
            "\"left_transform_set_found\":%s,"
            "\"left_transform_set_caller\":\"%08x\","
            "\"left_transform_set_source\":\"%08x\","
            "\"title_builder_found\":%s,"
            "\"title_builder_caller\":\"%08x\","
            "\"title_builder_this\":\"%08x\","
            "\"title_builder_translation\":\"%08x\","
            "\"title_builder_rotation\":\"%08x\","
            "\"title_builder_scale\":[%.9g,%.9g,%.9g],"
            "\"left_value\":[",
            step_index ? "," : "", step->output, step->caller, step->left,
            step->right, step->inputs_readable ? "true" : "false",
            step->left_copy_found ? "true" : "false", step->left_copy_caller,
            step->left_copy_source,
            step->left_copy_source_readable ? "true" : "false",
            step->left_transform_set_found ? "true" : "false",
            step->left_transform_set_caller, step->left_transform_set_source,
            step->title_builder_found ? "true" : "false",
            step->title_builder_caller, step->title_builder_this,
            step->title_builder_translation, step->title_builder_rotation,
            step->title_builder_scale[0], step->title_builder_scale[1],
            step->title_builder_scale[2]);
    for (value_index = 0; value_index < 16; ++value_index)
      fprintf(output, "%s%.9g", value_index ? "," : "",
              step->left_value[value_index]);
    fputs("],\"right_value\":[", output);
    for (value_index = 0; value_index < 16; ++value_index)
      fprintf(output, "%s%.9g", value_index ? "," : "",
              step->right_value[value_index]);
    fputs("],\"left_copy_source_value\":[", output);
    for (value_index = 0; value_index < 16; ++value_index)
      fprintf(output, "%s%.9g", value_index ? "," : "",
              step->left_copy_source_value[value_index]);
    fputs("]}", output);
  }
  fprintf(output, "],\"world_matrix_multiply_chain_truncated\":%s",
          evidence->world_matrix_multiply_chain_truncated ? "true" : "false");
}
