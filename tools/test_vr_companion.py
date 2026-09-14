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

    def test_blackbox_v2_retreat_forensics_are_aggregated(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "decision_begin", "decision_id": 1},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "assessment", "decision_id": 1,
             "assessment_type": "retreat_courage", "actor_id": 40,
             "hold_confidence": 72, "perceived_friendly_strength": 600,
             "perceived_enemy_strength": 300, "local_stress": 18,
             "stable_leader_nearby": True, "under_fire": False},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "candidate", "decision_id": 1,
             "candidate": "sector_escape", "eligible": False,
             "adjusted_score": 20},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "candidate", "decision_id": 1,
             "candidate": "organized_disengagement", "eligible": True,
             "adjusted_score": 42},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "formation_snapshot", "turn": 4, "team": 1,
             "living": 8, "combat_ready": 6, "escaping": 0,
             "disengaging": 1, "cowering": 0, "average_stress": 22,
             "casualty_percent": 35},
        ]
        decisions = companion.build_decisions(events)
        result = companion.tactical_summary(events, decisions)
        self.assertEqual(1, result["formation_snapshots"])
        self.assertEqual(75.0, result["avg_formation_ready_rate"])
        self.assertEqual(2.0, result["avg_perceived_force_ratio"])
        self.assertEqual(72.0, result["v2_state_means"]["hold_confidence"])
        self.assertEqual(1, result["retreat_eligible"]["organized_disengagement"])
        self.assertEqual(0, result["retreat_eligible"].get("sector_escape", 0))

    def test_blackbox_v2_keeps_repeated_assessments(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "decision_begin", "decision_id": 2},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "assessment", "decision_id": 2,
             "assessment_type": "retreat_courage",
             "hold_confidence": 80, "perceived_friendly_strength": 600,
             "perceived_enemy_strength": 300},
            {"schema": "vr-blackbox-1", "session": 10, "layer": "tactical",
             "kind": "assessment", "decision_id": 2,
             "assessment_type": "retreat_courage",
             "hold_confidence": 40, "perceived_friendly_strength": 300,
             "perceived_enemy_strength": 300},
        ]
        decisions = companion.build_decisions(events)
        result = companion.tactical_summary(events, decisions)
        self.assertEqual(60.0, result["v2_state_means"]["hold_confidence"])
        self.assertEqual(1.5, result["avg_perceived_force_ratio"])


    def test_grenade_throw_fairness_pairs_launch_and_landing(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "layer": "tactical", "kind": "grenade_throw_launch",
             "actor_id": 40, "projectile_id": 3, "item": 150,
             "target_distance": 18, "max_range": 20,
             "effective_strength": 78, "breath": 92, "breath_max": 100,
             "stance": 2, "throwing_traits": 0,
             "target_offset_to_nearest_player": 1},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "layer": "tactical", "kind": "grenade_throw_landing",
             "actor_id": 40, "projectile_id": 3, "item": 150,
             "actual_distance": 18, "landing_offset_to_nearest_player": 2},
        ]
        result = companion.grenade_throw_summary(events)
        self.assertEqual(1, result["paired_flights"])
        self.assertEqual(0, result["targeted_beyond_range"])
        self.assertEqual(0, result["actual_overrange"])
        self.assertEqual(20.0, result["avg_max_range"])
        self.assertEqual(18.0, result["avg_actual_distance"])

    def test_grenade_throw_fairness_flags_real_overrange(self):
        events = [
            {"schema": "vr-blackbox-1", "session": 10, "seq": 1,
             "layer": "tactical", "kind": "grenade_throw_launch",
             "actor_id": 41, "projectile_id": 4, "item": 150,
             "target_distance": 23, "max_range": 20,
             "effective_strength": 80, "breath": 100, "breath_max": 100,
             "stance": 2, "throwing_traits": 0},
            {"schema": "vr-blackbox-1", "session": 10, "seq": 2,
             "layer": "tactical", "kind": "grenade_throw_landing",
             "actor_id": 41, "projectile_id": 4, "item": 150,
             "actual_distance": 22, "landing_offset_to_nearest_player": 1},
        ]
        result = companion.grenade_throw_summary(events)
        self.assertEqual(1, result["targeted_beyond_range"])
        self.assertEqual(1, result["actual_overrange"])
        self.assertEqual(1, len(result["overrange_records"]))


if __name__ == "__main__":
    unittest.main()
