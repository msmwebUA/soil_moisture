#!/bin/bash
# Runs automatically once, the first time the postgres data volume is empty.
# Creates a dedicated role + database for ThingsBoard alongside the
# "chirpstack" one that POSTGRES_DB/POSTGRES_USER already created.
set -e

psql -v ON_ERROR_STOP=1 --username "$POSTGRES_USER" --dbname "$POSTGRES_DB" <<-EOSQL
    CREATE USER thingsboard WITH PASSWORD 'thingsboard';
    CREATE DATABASE thingsboard OWNER thingsboard;
    GRANT ALL PRIVILEGES ON DATABASE thingsboard TO thingsboard;
EOSQL