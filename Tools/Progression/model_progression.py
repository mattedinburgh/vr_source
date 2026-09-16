from __future__ import annotations
import argparse

def mastery_multiplier(rating: int) -> int:
    if rating <= 30:
        return 12500
    if rating <= 40:
        return 12500 - ((rating - 30) * 1000) // 10
    if rating <= 50:
        return 11500 - ((rating - 40) * 1000) // 10
    if rating <= 60:
        return 10500 - ((rating - 50) * 500) // 10
    if rating <= 75:
        return 10000
    if rating <= 80:
        return 10000 - ((rating - 75) * 500) // 5
    if rating <= 85:
        return 9500 - ((rating - 80) * 1000) // 5
    if rating <= 90:
        return 8500 - ((rating - 85) * 1000) // 5
    if rating <= 95:
        return 7500 - ((rating - 90) * 1500) // 5
    if rating <= 99:
        return 6000 - ((rating - 95) * 1000) // 4
    return 5000

def gain_probability(rating: int, wisdom: int | None) -> float:
    chance = max(0, 100 - rating)
    if wisdom is not None and chance:
        chance += (chance * (wisdom - 50)) // 100
    chance = min(99, chance)
    basis_points = min(9900, (chance * mastery_multiplier(rating)) // 100)
    return basis_points / 10000.0

def expected_awards(subpoints: int, probability: float) -> float:
    return float("inf") if probability <= 0 else subpoints / probability

def main() -> int:
    parser = argparse.ArgumentParser(description="Model VR stat progression.")
    parser.add_argument("--wisdom", type=int, default=80,
                        help="wisdom used for wisdom-affected skills")
    args = parser.parse_args()

    ratings = [20, 30, 40, 50, 60, 70, 75, 80, 85, 90, 95, 98, 99]
    print("rating,multiplier_pct,attribute_chance_pct,attribute_awards_per_point,"
          "skill_chance_pct,skill_awards_per_point")
    for rating in ratings:
        attr_p = gain_probability(rating, None)
        skill_p = gain_probability(rating, args.wisdom)
        print(
            f"{rating},{mastery_multiplier(rating)/100:.2f},"
            f"{attr_p*100:.3f},{expected_awards(50, attr_p):.1f},"
            f"{skill_p*100:.3f},{expected_awards(25, skill_p):.1f}"
        )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
