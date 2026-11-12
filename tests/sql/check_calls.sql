\set QUIET on
\pset tuples_only on
\pset format unaligned
SELECT (CASE WHEN count(*) = 1 THEN 'PASS' ELSE 'FAIL' END) || ': one run row with end_time set' FROM edumpi_runs WHERE end_time IS NOT NULL;
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': four cluster rows (' || count(*) || ')' FROM edumpi_cluster_info;
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Send 1000 B to the next rank, p2p (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Send' AND senddatasize=1000 AND partnerrank=(processrank+1)%4 AND communicationtype='p2p';
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Recv 1000 B from the previous rank (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Recv' AND recvdatasize=1000 AND partnerrank=(processrank+3)%4;
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Irecv 800 B, ends at the arrival, time_diff >= 0 (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Irecv' AND recvdatasize=800 AND time_end >= time_start AND time_diff >= 0;
SELECT (CASE WHEN count(*) = 8 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Allreduce rows are collective with an algorithm text (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Allreduce' AND communicationtype='collective' AND coll_algorithm <> '';
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Allreduce on the world has a 50 byte partner bitmap (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Allreduce' AND communicationarea='MPI_COMM_WORLD' AND length(coll_partnerranks)=50;
SELECT (CASE WHEN count(*) = 4 THEN 'PASS' ELSE 'FAIL' END) || ': MPI_Put is one-sided, 40 B (' || count(*) || ' rows)' FROM edumpi_running_data WHERE "function"='MPI_Put' AND communicationtype='one-sided' AND senddatasize=40;
SELECT (CASE WHEN count(*) = 0 THEN 'PASS' ELSE 'FAIL' END) || ': time_start is rounded to full seconds' FROM edumpi_running_data WHERE date_trunc('second', time_start) <> time_start;
SELECT (CASE WHEN count(*) = 0 THEN 'PASS' ELSE 'FAIL' END) || ': no row ends before it starts' FROM edumpi_running_data WHERE time_end < time_start - interval '1 second';
SELECT (CASE WHEN count(*) = 0 THEN 'PASS' ELSE 'FAIL' END) || ': processorname and rank filled' FROM edumpi_running_data WHERE processorname = '' OR processrank NOT BETWEEN 0 AND 3;
SELECT 'INFO: rows per function: ' || string_agg("function" || '=' || n, ' ' ORDER BY "function") FROM (SELECT "function", count(*) n FROM edumpi_running_data GROUP BY 1) q;
SELECT 'INFO: algorithms: ' || string_agg(DISTINCT "function" || ' -> ' || coll_algorithm, '; ') FROM edumpi_running_data WHERE communicationtype='collective' AND coll_algorithm <> '';
