/* Oh. My. God. */

#include <stdlib.h>
#include <unistd.h>

int main (int argc, char** argv)
{
  if (argc > 1)
    sleep(atoi(argv[1]));
  return 0;
}
