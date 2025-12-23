#include <re.h>
#include <stdio.h>

int main(void) {
  int err;
  struct sa sa;
  const char *addr = "233.5.5.5";

  // Initialize libre (sometimes needed for net, but sa_decode might be
  // standalone?) sa_decode usually just parsing.

  err = sa_decode(&sa, addr, str_len(addr));
  printf("Decode '%s': %d (%m)\n", addr, err, err);

  const char *addr_port = "233.5.5.5:53";
  err = sa_decode(&sa, addr_port, str_len(addr_port));
  printf("Decode '%s': %d (%m)\n", addr_port, err, err);

  return 0;
}
