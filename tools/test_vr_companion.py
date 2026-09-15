#!/usr/bin/env python3
import unittest

import vr_companion_analyze as companion


class CompanionAnalysisTests(unittest.TestCase):
    def test_decision_ids_are_scoped_by_session(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "decision_begin", "decision_id": 1},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "decision_commit", "decision_id": 1, "action": 4},
            {"schema": "vr-blackbox-1", "session": 11, "layer": "tactical",
             "kind": "decision_begin", "decision_id": 1},
            {"schema": "vr-blackbox-1", "session": 11, "layer": "tactical",
             "kind": "decision_commit", "decision_id": 1, "action": 7},
        ]
        decisions = companion.build_decisions(events)
        self.assertEqual(2, len(decisions))
        self.assertIn((10, 1), decisions)
        self.assertIn((11, 1), decisions)

    def test_strategic_move_order_pairs_with_arrival(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "kind": "strategic_move_order", "group_id": 7,
             "source_sector": 0, "target_sector": 17,
             "world_minutes": 100, "group_size": 12},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "kind": "strategic_group_arrived", "group_id": 7,
             "sector": 17, "world_minutes": 160, "group_size": 12,
             "assignment": "garrison_reinforcement"},
        ]
        result = companion.strategic_mobility_summary(events)
        self.assertEqual(1, result["matched_arrivals"])
        self.assertEqual(100.0, result["arrival_match_rate"])
        self.assertEqual(60.0, result["avg_travel_minutes"])

    def test_redirect_uses_latest_matching_order(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "kind": "strategic_move_order", "group_id": 7,
             "target_sector": 17, "world_minutes": 100},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "kind": "strategic_move_order", "group_id": 7,
             "target_sector": 18, "world_minutes": 120},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 3,
             "kind": "strategic_group_arrived", "group_id": 7,
             "sector": 18, "world_minutes": 150,
             "assignment": "garrison_reinforcement"},
        ]
        result = companion.strategic_mobility_summary(events)
        self.assertEqual(1, result["matched_arrivals"])
        self.assertEqual(30.0, result["avg_travel_minutes"])

    def test_reinforcement_arrival_links_to_later_battle(self):
        # Sector B2 -> zero-based strategic sector 17.
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "kind": "strategic_group_arrived", "group_id": 7,
             "sector": 17, "world_minutes": 100, "group_size": 12,
             "assignment": "garrison_reinforcement"},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "kind": "battle_start", "battle_id": 1,
             "sector_x": 2, "sector_y": 2, "sector_z": 0,
             "world_minutes": 200, "enemy_count": 18},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 3,
             "kind": "battle_end", "battle_id": 1,
             "result": "victory", "world_minutes": 250},
        ]
        result = companion.interaction_summary(events)
        self.assertEqual(1, result["reinforced_battles"])
        self.assertEqual(12.0, result["avg_recent_reinforcement_troops"])
        self.assertEqual(100.0, result["reinforced_player_success_rate"])

    def test_old_reinforcement_is_not_attributed_to_battle(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "kind": "strategic_group_arrived", "group_id": 7,
             "sector": 17, "world_minutes": 100, "group_size": 12},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "kind": "battle_start", "battle_id": 1,
             "sector_x": 2, "sector_y": 2, "sector_z": 0,
             "world_minutes": 4000},
        ]
        result = companion.interaction_summary(events)
        self.assertEqual(0, result["reinforced_battles"])
        self.assertEqual(1, result["unreinforced_battles"])


if __name__ == "__main__":
    unittest.main()
