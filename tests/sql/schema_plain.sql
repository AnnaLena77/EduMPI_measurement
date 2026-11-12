-- the tables of EduStore/edustore_schema.sql of the EduMPI repository without TimescaleDB (hypertable and
-- continuous aggregates are not needed to test the writer)
CREATE TABLE edumpi_runs (edumpi_run_id SERIAL PRIMARY KEY, start_time TIMESTAMPTZ NOT NULL, end_time TIMESTAMPTZ NULL,
    user_id VARCHAR NULL, program_name VARCHAR NULL);
CREATE TABLE edumpi_cluster_info (edumpi_cluster_id SERIAL PRIMARY KEY, edumpi_run_id INT REFERENCES edumpi_runs(edumpi_run_id),
    processorname TEXT NULL, processrank INT4 NULL);
CREATE TABLE edumpi_running_data (edumpi_run_id INT REFERENCES edumpi_runs(edumpi_run_id), "function" TEXT NULL,
    communicationtype TEXT NULL, senddatasize BIGINT NULL, recvdatasize BIGINT NULL, communicationarea TEXT NULL,
    processorname TEXT NULL, processrank INT4 NULL, partnerrank INT4 NULL, coll_algorithm TEXT NULL,
    time_start TIMESTAMPTZ NOT NULL, time_end TIMESTAMPTZ NOT NULL, latesendertime DOUBLE PRECISION NULL,
    laterecvrtime DOUBLE PRECISION NULL, time_diff DOUBLE PRECISION NULL, coll_partnerranks BYTEA NULL);
