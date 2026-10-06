/* Control-route query values: found by exact name and form-decoded, so a
   key whose name has a space ("Keypad 6") can be pressed. */
#include "control_query.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check(int ok, const char *what) {
  if (!ok) {
    printf("FAIL: %s\n", what);
    failures++;
  }
}

static int value_is(const char *query, const char *name, const char *want) {
  char out[32];
  return control_query_arg(query, name, out, sizeof out) &&
         strcmp(out, want) == 0;
}

int main(void) {
  char small[4];

  check(value_is("name=Return&hold=0.1", "name", "Return"),
        "a plain value is copied");
  check(value_is("name=Return&hold=0.1", "hold", "0.1"),
        "a later value is found");
  check(value_is("name=Keypad+6", "name", "Keypad 6"), "'+' is a space");
  check(value_is("name=Keypad%206", "name", "Keypad 6"), "%20 is a space");
  check(value_is("name=a%2Bb", "name", "a+b"), "%2B is a literal plus");
  check(value_is("name=50%", "name", "50%"), "a trailing '%' is kept");
  check(value_is("name=%zz", "name", "%zz"), "a '%' without hex is kept");
  check(value_is("name=%2", "name", "%2"), "a cut-off escape is kept");
  check(value_is("xname=1&name=2", "name", "2"),
        "a name is matched whole, not as a suffix");
  check(!control_query_arg("hold=1", "name", small, sizeof small),
        "a missing name is refused");
  check(control_query_arg("name=abcdef", "name", small, sizeof small) &&
            strcmp(small, "abc") == 0,
        "a long value is truncated to fit");

  if (failures) {
    printf("%d failure(s)\n", failures);
    return 1;
  }
  printf("control query: ok\n");
  return 0;
}
