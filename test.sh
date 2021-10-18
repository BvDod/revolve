#!/usr/bin/env bash

set -u

N=1
CORES=4
MANAGER_PATH="experiments/EC_students/manager_population_cppn.py"
EVALUATION_TIME=30
EXPERIMENT_NAME="linear_"
PORT_START=11080

set -x

cd $HOME/git/revolve

total_start=$(date +%s)

for alpha in $(seq 0.1 0.1 0.9); do
    EXPERIMENT_NAME_RUN="$EXPERIMENT_NAME$alpha"
    echo "$EXPERIMENT_NAME_RUN"
    for i in $(seq 1 $N); do
        echo "---------------- RUN $i/$N -----------------"
        ulimit -Sn 4096
        run_start=$(date +%s)
        until ./revolve.py \
            --simulator-cmd gzserver \
            --run $i \
            --experiment-name $EXPERIMENT_NAME_RUN \
            --manager $MANAGER_PATH \
            --n-cores $CORES \
            --port-start $PORT_START \
            --evaluation-time $EVALUATION_TIME \
            --line_alpha_start $alpha; do
            sleep 1;
    done
done


  run_end=$(date +%s)
  run_time=$((run_end-run_start))
  echo "###########################################"
  echo "RUN DURATION: $((run_time/60)) minutes"
  echo "###########################################"
done

total_end=$(date +%s)
total_time=$((run_end-run_start))
echo "###########################################"
echo "TOTAL DURATION: $((total_time/60)) minutes"
echo "###########################################"
