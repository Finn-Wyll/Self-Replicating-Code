#!/bin/bash
cd /home/user/Self-Replicating-Code
L=${LIBRARY:-benchmarks/strains}   # contains courier-picker-v1
D=examples/data/couriers_premium.csv
acc() { python3 -c "import json,sys; r=[json.loads(l) for l in sys.stdin][-1]; print(r['test_metrics']['accuracy'], r['test_metrics']['linear_model_accuracy'], r['test_metrics']['best_single_feature_accuracy'])"; }
for s in 1 2 3 4 5 6 7 8 9 10; do
  before=$(./build/adaptogen adapt courier-picker-v1 --library $L --data $D --only-new --seed $s --generations 1 2>&1 | grep "before:" | grep -o " accuracy=[0-9.e-]*" | head -1)
  scratch=$(./build/adaptogen evolve --problem decisions --param data=$D --seed $s --generations 300 --threads 4 --json --no-simplify | acc)
  seeded=$(./build/adaptogen evolve --problem decisions --param data=$D --seed $s --generations 300 --threads 4 --json --no-simplify --seed-strain courier-picker-v1 --library $L | acc)
  echo "seed $s | unadapted $before | scratch $scratch | adapted $seeded"
done
echo PREMDONE
