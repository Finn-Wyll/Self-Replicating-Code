"""Generate examples/data/couriers.csv: choosing a delivery courier for each order.

Each order (case) has a deadline (context.deadline_hours). Each courier option has a price, an
ETA and a rating. The outcome (reward) is customer satisfaction: a late delivery is bad whatever
it costs; among couriers that make the deadline, cheaper and better-rated is better. That
interaction (ETA versus *this order's* deadline) is what simple rules miss.

Deterministic: python3 examples/data/make_couriers.py
A business with different priorities (here: a premium shop whose customers care much more about
ratings than price), with only a little data:
    python3 examples/data/make_couriers.py --seed 11 --cases 150 --rating-weight 4 --price-weight 0.1 --out couriers_premium.csv
"""
import argparse
import csv
import os
import random

ap = argparse.ArgumentParser()
ap.add_argument("--seed", type=int, default=7)
ap.add_argument("--cases", type=int, default=800)
ap.add_argument("--rating-weight", type=float, default=1.5)
ap.add_argument("--price-weight", type=float, default=0.25)
ap.add_argument("--out", default="couriers.csv")
args = ap.parse_args()
random.seed(args.seed)
rows = []
for case in range(args.cases):
    deadline = random.choice([2, 4, 6, 8, 24])
    for k in range(random.randint(3, 6)):
        price = round(random.uniform(3, 25), 2)
        eta = round(random.uniform(0.5, 30), 1)
        rating = round(random.uniform(2.5, 5.0), 1)
        on_time = eta <= deadline
        satisfaction = (8.0 if on_time else 0.0) + args.rating_weight * rating - args.price_weight * price + random.gauss(0, 0.4)
        rows.append([case, price, eta, rating, deadline, round(satisfaction, 3), f"courier-{k}"])

path = os.path.join(os.path.dirname(__file__), args.out)
with open(path, "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["case", "price", "eta_hours", "rating", "context.deadline_hours", "reward", "option"])
    w.writerows(rows)
print(f"wrote {path}: {len(rows)} options in {args.cases} decisions")
