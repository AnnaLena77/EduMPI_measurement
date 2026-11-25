/*
 * Testprogramm für den EduMPI-Dev-Cluster: erzeugt viel und wechselnde Kommunikation über
 * alle Nodes, damit Zeitleiste, Matrix und 3D-Ansicht der GUI deutlich zeigen, wer mit wem
 * spricht. Die Phasen wechseln reihum (Standard: alle 5 s, insgesamt 60 s):
 *
 *   Ring            jeder Rank sendet an den rechten Nachbarn (Irecv + Send + Wait)
 *   Verschiebung    Partner wechselt jede Runde: rank+1, rank+2, ... (Isend/Irecv/Waitall)
 *   Hotspot         alle senden an Rank 0, Rank 0 antwortet (Stern)
 *   Zufallspaare    jede Runde neue, zufällige Paare (Send/Recv)
 *   Kollektive      Bcast, Reduce, Allreduce, Allgather, Alltoall, Scatter, Gather, Barrier
 *   Ungleichgewicht Ranks rechnen unterschiedlich lang, gerade Ranks senden verspätet
 *                   (Wartezeiten / Late Sender)
 *
 *   mpicc edumpi_test.c -o edumpi_test
 *   mpirun -n 8 ./edumpi_test [Sekunden gesamt] [Sekunden je Phase]   (Standard: 60 5)
 *
 * Nicht für Performance-Aussagen gedacht – nur um zu sehen, dass alles kommuniziert.
 * Wichtig: MPI_Init, nicht MPI_Init_thread – nur MPI_Init startet das EduMPI-Logging.
 */
#include <mpi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_MSG (256 * 1024)
#define NPHASES 6

static const char *phase_names[NPHASES] = {
    "Ring", "Verschiebung", "Hotspot", "Zufallspaare", "Kollektive", "Ungleichgewicht",
};

static int rank, size;
static char *sbuf, *rbuf, *a2a_s, *a2a_r;
static long long sent_msgs, sent_bytes;

static void send_to(int dst, int bytes, int tag)
{
    MPI_Send(sbuf, bytes, MPI_CHAR, dst, tag, MPI_COMM_WORLD);
    sent_msgs++;
    sent_bytes += bytes;
}

/* Ring: Irecv vom linken Nachbarn, Send an den rechten – ohne Deadlock bei jeder Rank-Zahl */
static void phase_ring(int iter)
{
    int right = (rank + 1) % size, left = (rank - 1 + size) % size;
    int bytes = 1024 << (iter % 8); /* 1 KiB .. 128 KiB */
    for (int r = 0; r < 4; r++) {
        MPI_Request req;
        MPI_Irecv(rbuf, bytes, MPI_CHAR, left, r, MPI_COMM_WORLD, &req);
        send_to(right, bytes, r);
        MPI_Wait(&req, MPI_STATUS_IGNORE);
    }
}

/* Verschiebung: Abstand k wechselt jede Runde, so sprechen nach und nach alle Paare */
static void phase_shift(int iter)
{
    if (size < 2)
        return;
    int k = 1 + iter % (size - 1);
    int dst = (rank + k) % size, src = (rank - k + size) % size;
    int bytes = 4096 << (iter % 5); /* 4 KiB .. 64 KiB */
    MPI_Request req[2];
    MPI_Irecv(rbuf, bytes, MPI_CHAR, src, 10, MPI_COMM_WORLD, &req[0]);
    MPI_Isend(sbuf, bytes, MPI_CHAR, dst, 10, MPI_COMM_WORLD, &req[1]);
    sent_msgs++;
    sent_bytes += bytes;
    MPI_Waitall(2, req, MPI_STATUSES_IGNORE);
}

/* Hotspot: alle an Rank 0, Rank 0 antwortet jedem einzeln */
static void phase_hotspot(int iter)
{
    int bytes = 2048 + 1024 * (iter % 4);
    if (rank == 0) {
        for (int i = 1; i < size; i++) {
            MPI_Status st;
            MPI_Recv(rbuf, MAX_MSG, MPI_CHAR, MPI_ANY_SOURCE, 20, MPI_COMM_WORLD, &st);
            send_to(st.MPI_SOURCE, bytes, 21);
        }
    } else {
        send_to(0, bytes, 20);
        MPI_Recv(rbuf, bytes, MPI_CHAR, 0, 21, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
}

/* Zufallspaare: alle Ranks erzeugen dieselbe Permutation (gleicher Seed je Runde) */
static void phase_random_pairs(int iter)
{
    int perm[size];
    for (int i = 0; i < size; i++)
        perm[i] = i;
    unsigned int seed = 2654435761u * (unsigned int)(iter + 1);
    for (int i = size - 1; i > 0; i--) {
        seed = seed * 1103515245u + 12345u;
        int j = (int)((seed >> 16) % (unsigned int)(i + 1));
        int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
    }
    int bytes = 64 * 1024;
    for (int p = 0; p + 1 < size; p += 2) {
        int a = perm[p], b = perm[p + 1];
        if (rank == a) {
            send_to(b, bytes, 30);
            MPI_Recv(rbuf, bytes, MPI_CHAR, b, 30, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        } else if (rank == b) {
            MPI_Recv(rbuf, bytes, MPI_CHAR, a, 30, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            send_to(a, bytes, 30);
        }
    }
}

static void phase_collectives(int iter)
{
    int root = iter % size;
    MPI_Bcast(sbuf, 64 * 1024, MPI_CHAR, root, MPI_COMM_WORLD);

    double vals[128], res[128];
    for (int i = 0; i < 128; i++)
        vals[i] = rank + i + iter;
    MPI_Reduce(vals, res, 128, MPI_DOUBLE, MPI_SUM, root, MPI_COMM_WORLD);
    MPI_Allreduce(vals, res, 128, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);

    MPI_Allgather(sbuf, 1024, MPI_CHAR, a2a_r, 1024, MPI_CHAR, MPI_COMM_WORLD);
    MPI_Alltoall(a2a_s, 2048, MPI_CHAR, a2a_r, 2048, MPI_CHAR, MPI_COMM_WORLD);
    MPI_Scatter(a2a_s, 1024, MPI_CHAR, rbuf, 1024, MPI_CHAR, root, MPI_COMM_WORLD);
    MPI_Gather(sbuf, 1024, MPI_CHAR, a2a_r, 1024, MPI_CHAR, root, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);
}

/* Ungleichgewicht: höhere Ranks "rechnen" länger; gerade Ranks senden verspätet */
static void phase_imbalance(int iter)
{
    usleep((useconds_t)(1000 * (rank + 1)));
    int partner = (rank % 2 == 0) ? rank + 1 : rank - 1;
    if (partner < size) {
        int bytes = 16 * 1024;
        if (rank % 2 == 0) {
            usleep(3000); /* Late Sender: der Empfänger wartet im Recv */
            send_to(partner, bytes, 40 + iter % 2);
        } else {
            MPI_Recv(rbuf, bytes, MPI_CHAR, partner, 40 + iter % 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
    }
    double v = rank, sum;
    MPI_Allreduce(&v, &sum, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD); /* Wartezeit der Schnellen */
}

int main(int argc, char **argv)
{
    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    double seconds = argc > 1 ? atof(argv[1]) : 60.0;
    double phase_len = argc > 2 ? atof(argv[2]) : 5.0;
    if (phase_len <= 0)
        phase_len = 5.0;

    sbuf = calloc(MAX_MSG, 1);
    rbuf = calloc(MAX_MSG, 1);
    a2a_s = calloc((size_t)size * 2048, 1);
    a2a_r = calloc((size_t)size * 2048, 1);

    char host[MPI_MAX_PROCESSOR_NAME];
    int len;
    MPI_Get_processor_name(host, &len);
    printf("Rank %d/%d auf %s\n", rank, size, host);
    fflush(stdout);
    MPI_Barrier(MPI_COMM_WORLD);

    double start = MPI_Wtime();
    int iter = 0, last_phase = -1;
    long long phase_iters[NPHASES] = {0};
    while (1) {
        /* Rank 0 gibt Ende und Phase vor, damit alle Ranks dasselbe Muster ausführen */
        int ctrl[2];
        if (rank == 0) {
            double t = MPI_Wtime() - start;
            ctrl[0] = t >= seconds;
            ctrl[1] = (int)(t / phase_len) % NPHASES;
        }
        MPI_Bcast(ctrl, 2, MPI_INT, 0, MPI_COMM_WORLD);
        if (ctrl[0])
            break;

        int phase = ctrl[1];
        if (rank == 0 && phase != last_phase) {
            time_t now = time(NULL);
            char ts[16];
            strftime(ts, sizeof ts, "%H:%M:%S", localtime(&now));
            printf("[%s, t=%5.1f s] Phase: %s\n", ts, MPI_Wtime() - start, phase_names[phase]);
            fflush(stdout);
        }
        last_phase = phase;

        switch (phase) {
        case 0: phase_ring(iter); break;
        case 1: phase_shift(iter); break;
        case 2: phase_hotspot(iter); break;
        case 3: phase_random_pairs(iter); break;
        case 4: phase_collectives(iter); break;
        case 5: phase_imbalance(iter); break;
        }
        phase_iters[phase]++;

        /* kurze Pause: viel Verkehr, aber der EduMPI-Ringpuffer kommt hinterher */
        usleep(2000);
        iter++;
    }

    long long totals[2] = {sent_msgs, sent_bytes}, all[2];
    MPI_Reduce(totals, all, 2, MPI_LONG_LONG, MPI_SUM, 0, MPI_COMM_WORLD);
    if (rank == 0) {
        printf("Fertig nach %.1f s, %d Runden:", MPI_Wtime() - start, iter);
        for (int p = 0; p < NPHASES; p++)
            printf(" %s=%lld", phase_names[p], phase_iters[p]);
        printf("\nPunkt-zu-Punkt gesamt: %lld Nachrichten, %.1f MiB\n", all[0], all[1] / 1048576.0);
    }

    free(sbuf);
    free(rbuf);
    free(a2a_s);
    free(a2a_r);
    MPI_Finalize();
    return 0;
}
