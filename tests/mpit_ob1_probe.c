/*
 * WP-0 probe: register every MPI_T event whose name starts with a prefix (default
 * "ompi.pml_ob1_") on MPI_COMM_WORLD, print each event with its timestamp and payload, and run
 * two ping-pong scenarios between rank 0 and rank 1:
 *
 *   late_sender   : rank 1 posts the receive, rank 0 sends DELAY seconds later
 *   late_receiver : rank 0 sends, rank 1 posts the receive DELAY seconds later
 *
 * for an eager size (8 bytes) and a rendezvous size (4 MiB), blocking and non-blocking receive.
 *
 * Build: mpicc -O1 -o mpit_ob1_probe mpit_ob1_probe.c
 * Run:   mpirun -n 2 --tag-output --mca pml ob1 ./mpit_ob1_probe [prefix] [delay_seconds]
 *        env PROBE_SKIP=search_  skips event names containing that string (default: none)
 *
 * Output lines:  EV <rank> <t_ns> <event> <elem0> <elem1> ...   and   SC <rank> <t_ns> <text>
 * Times are in the event source's ticks; "SC" lines use the same source via
 * MPI_T_source_get_timestamp so they can be ordered against the events.
 */
#include <mpi.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAXEL 16

typedef struct {
    char name[256];
    int n;
    MPI_Datatype types[MAXEL];
    int src;
} evdesc_t;

static int g_rank;
static int g_src = 0;

static long long now_ticks(void)
{
    MPI_Count t = 0;
    MPI_T_source_get_timestamp(g_src, &t);
    return (long long) t;
}

static void cb(MPI_T_event_instance ev, MPI_T_event_registration reg, MPI_T_cb_safety safety, void *user)
{
    evdesc_t *d = (evdesc_t *) user;
    MPI_Count ts = 0;
    char line[512];
    int off = 0;
    (void) reg;
    (void) safety;
    MPI_T_event_get_timestamp(ev, &ts);
    off += snprintf(line + off, sizeof(line) - off, "EV %d %lld %s", g_rank, (long long) ts, d->name);
    for (int i = 0; i < d->n && off < (int) sizeof(line) - 32; ++i) {
        int sz = 0;
        unsigned char buf[16] = {0};
        MPI_Type_size(d->types[i], &sz);
        MPI_T_event_read(ev, i, buf);
        if (8 == sz) {
            int64_t v;
            memcpy(&v, buf, 8);
            off += snprintf(line + off, sizeof(line) - off, " %lld", (long long) v);
        } else if (4 == sz) {
            int32_t v;
            memcpy(&v, buf, 4);
            off += snprintf(line + off, sizeof(line) - off, " %d", (int) v);
        } else {
            off += snprintf(line + off, sizeof(line) - off, " ?%d", sz);
        }
    }
    puts(line);
}

static void mark(const char *text)
{
    printf("SC %d %lld %s\n", g_rank, now_ticks(), text);
    fflush(stdout);
}

static void scenario(const char *label, int late_sender, size_t bytes, int nonblocking, int delay)
{
    char text[256];
    char *buf = malloc(bytes);
    MPI_Request req;
    memset(buf, 1, bytes);
    snprintf(text, sizeof(text), "BEGIN %s bytes=%zu %s", label, bytes, nonblocking ? "irecv" : "recv");
    MPI_Barrier(MPI_COMM_WORLD);
    mark(text);
    if (0 == g_rank) {
        if (late_sender) {
            sleep(delay);
        }
        mark("rank0 send");
        MPI_Send(buf, (int) bytes, MPI_BYTE, 1, 42, MPI_COMM_WORLD);
        mark("rank0 send returned");
    } else if (1 == g_rank) {
        if (!late_sender) {
            sleep(delay);
        }
        mark("rank1 post receive");
        if (nonblocking) {
            MPI_Irecv(buf, (int) bytes, MPI_BYTE, 0, 42, MPI_COMM_WORLD, &req);
            mark("rank1 irecv returned");
            MPI_Wait(&req, MPI_STATUS_IGNORE);
        } else {
            MPI_Recv(buf, (int) bytes, MPI_BYTE, 0, 42, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
        mark("rank1 receive completed");
    }
    MPI_Barrier(MPI_COMM_WORLD);
    snprintf(text, sizeof(text), "END %s", label);
    mark(text);
    free(buf);
}

int main(int argc, char **argv)
{
    const char *prefix = argc > 1 ? argv[1] : "ompi.pml_ob1_";
    int delay = argc > 2 ? atoi(argv[2]) : 1;
    const char *skip = getenv("PROBE_SKIP");
    int provided, nev, registered = 0;
    MPI_Comm world = MPI_COMM_WORLD;

    MPI_Init(&argc, &argv);
    MPI_T_init_thread(MPI_THREAD_MULTIPLE, &provided);
    MPI_Comm_rank(MPI_COMM_WORLD, &g_rank);
    setvbuf(stdout, NULL, _IOLBF, 0);

    MPI_T_event_get_num(&nev);
    if (0 == g_rank) {
        printf("INFO %d events in total, prefix '%s'\n", nev, prefix);
    }
    for (int i = 0; i < nev; ++i) {
        evdesc_t *d = calloc(1, sizeof(*d));
        int nlen = sizeof(d->name), verb, nel = MAXEL, bind, dlen = 0;
        MPI_Aint disp[MAXEL];
        MPI_T_enum en;
        MPI_Info info;
        MPI_T_event_registration reg;
        if (MPI_SUCCESS != MPI_T_event_get_info(i, d->name, &nlen, &verb, d->types, disp, &nel, &en,
                                                &info, NULL, &dlen, &bind)) {
            free(d);
            continue;
        }
        d->n = nel;
        if (0 != strncmp(d->name, prefix, strlen(prefix)) || (skip && strstr(d->name, skip))) {
            free(d);
            continue;
        }
        if (0 == g_rank) {
            printf("INFO event %d %s elements=%d bind=%d\n", i, d->name, nel, bind);
        }
        /* bound events: obj_handle is the ADDRESS of the handle variable */
        if (MPI_SUCCESS == MPI_T_event_handle_alloc(i, &world, MPI_INFO_NULL, &reg)
            && MPI_SUCCESS == MPI_T_event_register_callback(reg, MPI_T_CB_REQUIRE_NONE, MPI_INFO_NULL, d, cb)) {
            ++registered;
        } else {
            free(d);
        }
    }
    if (0 == g_rank) {
        printf("INFO registered %d events\n", registered);
    }

    scenario("late_sender", 1, 8, 0, delay);
    scenario("late_sender", 1, 8, 1, delay);
    scenario("late_sender", 1, 4 << 20, 0, delay);
    scenario("late_receiver", 0, 8, 0, delay);
    scenario("late_receiver", 0, 8, 1, delay);
    scenario("late_receiver", 0, 4 << 20, 0, delay);

    MPI_T_finalize();
    MPI_Finalize();
    return 0;
}
