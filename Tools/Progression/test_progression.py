from model_progression import gain_probability, mastery_multiplier

def main() -> int:
    assert mastery_multiplier(30) == 12500
    assert mastery_multiplier(60) == 10000
    assert mastery_multiplier(75) == 10000
    assert mastery_multiplier(90) == 7500
    assert mastery_multiplier(95) == 6000
    assert mastery_multiplier(99) == 5000

    multipliers = [mastery_multiplier(r) for r in range(30, 100)]
    assert all(a >= b for a, b in zip(multipliers, multipliers[1:]))

    attr_prob = [gain_probability(r, None) for r in range(1, 100)]
    assert all(a >= b for a, b in zip(attr_prob, attr_prob[1:]))
    assert gain_probability(99, None) >= 0.005
    assert gain_probability(99, 80) >= 0.005
    assert gain_probability(20, None) <= 0.99

    print("Progression regression checks passed.")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
