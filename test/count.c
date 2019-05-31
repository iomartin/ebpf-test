/*
 * This program counts how many times a byte appears in an array.
 *
 * The eBPF specification says that the entry function can only have one
 * parameter.  Thus, we encapsulate everything in a single data structure, with
 * the following layout:
 *   - length: int
 *   - key:    char
 *   - data:   char*
 */
int count(void *mem)
{

    int length = ((int *) mem)[0];
    char key = ((char *) mem)[4];
    char *data = &((char *) mem)[5];

    int count = 0;

    for (int i = 0; i < length; i++)
        count += (data[i] == key);

    return count;
}
