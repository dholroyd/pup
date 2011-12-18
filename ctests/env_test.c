#include "../env.h"
#include "../abortf.h"

int main(int argc, char **argv)
{
	struct RuntimeEnv *env = pup_runtime_env_create();

	pup_runtime_env_destroy(env);
}
