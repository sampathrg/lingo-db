#!/bin/bash

# Set the LINGODB environment variables
export LINGODB_EXECUTION_MODE=DEFAULT
export LINGODB_PARALLELISM=32
export QUERY_RUNS=5
export LINGODB_PRINT_TYPE=csv

file_names=("11" "12" "13" "21" "22" "23" "31" "32" "33" "34" "41" "42" "43")
# file_names=("41" "42" "43")
for file_name in "${file_names[@]}"; do
  echo "Running query: $file_name"
  $1/run-sql $2/$file_name.sql $3
  echo
done
