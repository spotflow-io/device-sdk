/* ELF metadata only: do not allocate space in the firmware memory image. */
.spotflow.compact_logs 0 (INFO) :
{
    KEEP(*(.spotflow.compact_logs))
}
