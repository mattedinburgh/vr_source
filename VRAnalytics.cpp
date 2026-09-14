#include "VRAnalytics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include ".generated/VRBuildInfo.generated.h"

namespace
{
	struct TacticalDecisionTrace
	{
		unsigned long decisionId;
		int action;
		long startGrid;
		int startAP;
		int startLife;
		int startBreath;
		bool selected;

		TacticalDecisionTrace()
			: decisionId(0),
			  action(0),
			  startGrid(0),
			  startAP(0),
			  startLife(0),
			  startBreath(0),
			  selected(false)
		{
		}
	};

	bool gEnabled = true;
	bool gInitialized = false;
	FILE* gFile = NULL;
	unsigned long gSequence = 0;
	unsigned long gDecisionSequence = 0;
	unsigned long gBattleSequence = 0;
	unsigned long gSessionId = 0;
	unsigned long gActiveBattleId = 0;
	int gBattleSectorX = 0;
	int gBattleSectorY = 0;
	int gBattleSectorZ = 0;
	int gBattleStartPlayers = 0;
	int gBattleStartEnemies = 0;
	int gBattleStartMilitia = 0;
	TacticalDecisionTrace gTacticalTrace[256];

	struct DecisionMeta
	{
		unsigned long id;
		VRAnalyticsLayer layer;
		DecisionMeta() : id(0), layer(VR_ANALYTICS_TACTICAL) {}
	};
	const unsigned int DECISION_META_CAPACITY = 8192;
	DecisionMeta gDecisionMeta[DECISION_META_CAPACITY];

	VRAnalyticsLayer DecisionLayer( unsigned long decisionId )
	{
		DecisionMeta& meta = gDecisionMeta[ decisionId % DECISION_META_CAPACITY ];
		if( meta.id == decisionId )
			return meta.layer;
		return VR_ANALYTICS_TACTICAL;
	}

	void ReadExperimentTag( char* buffer, size_t bufferSize )
	{
		const char* env;
		FILE* tagFile;
		size_t len;

		if( !buffer || bufferSize == 0 )
			return;

		buffer[0] = '\0';
		env = getenv( "VR_ANALYTICS_EXPERIMENT" );
		if( env && env[0] )
		{
			strncpy( buffer, env, bufferSize - 1 );
			buffer[bufferSize - 1] = '\0';
			return;
		}

		tagFile = fopen( "VR_Analytics_Experiment.txt", "rb" );
		if( tagFile )
		{
			if( fgets( buffer, (int)bufferSize, tagFile ) )
			{
				len = strlen( buffer );
				while( len && (buffer[len - 1] == '\r' || buffer[len - 1] == '\n') )
					buffer[--len] = '\0';
			}
			fclose( tagFile );
		}

		if( !buffer[0] )
		{
			strncpy( buffer, "unlabeled", bufferSize - 1 );
			buffer[bufferSize - 1] = '\0';
		}
	}

	const char* LayerName( VRAnalyticsLayer layer )
	{
		switch( layer )
		{
			case VR_ANALYTICS_TACTICAL: return "tactical";
			case VR_ANALYTICS_STRATEGIC: return "strategic";
			default: return "unknown";
		}
	}

	void JsonString( FILE* file, const char* value )
	{
		const unsigned char* p;

		if( !value )
		{
			fputs( "null", file );
			return;
		}

		fputc( '"', file );
		p = (const unsigned char*)value;
		while( *p )
		{
			switch( *p )
			{
				case '"': fputs( "\\\"", file ); break;
				case '\\': fputs( "\\\\", file ); break;
				case '\b': fputs( "\\b", file ); break;
				case '\f': fputs( "\\f", file ); break;
				case '\n': fputs( "\\n", file ); break;
				case '\r': fputs( "\\r", file ); break;
				case '\t': fputs( "\\t", file ); break;
				default:
					if( *p < 32 )
					{
						fprintf( file, "\\u%04x", (unsigned int)*p );
					}
					else
					{
						fputc( *p, file );
					}
					break;
			}
			++p;
		}
		fputc( '"', file );
	}

	void Initialize()
	{
		if( gInitialized )
			return;

		gInitialized = true;
		gSessionId = (unsigned long)time( NULL );

		if( !gEnabled )
			return;

		gFile = fopen( "VR_BlackBox.jsonl", "ab" );
		if( !gFile )
		{
			gEnabled = false;
			return;
		}

		char experimentTag[128];
		ReadExperimentTag( experimentTag, sizeof( experimentTag ) );

		++gSequence;
		fprintf( gFile,
			"{\"schema\":\"vr-blackbox-1\",\"seq\":%lu,\"session\":%lu,"
			"\"layer\":\"system\",\"kind\":\"session_start\","
			"\"build_date\":",
			gSequence, gSessionId );
		JsonString( gFile, __DATE__ );
		fputs( ",\"build_time\":", gFile );
		JsonString( gFile, __TIME__ );
		fputs( ",\"experiment_tag\":", gFile );
		JsonString( gFile, experimentTag );
		fputs( ",\"build_branch\":", gFile );
		JsonString( gFile, VR_BUILD_BRANCH );
		fputs( ",\"build_commit\":", gFile );
		JsonString( gFile, VR_BUILD_COMMIT );
		fputs( ",\"build_commit_short\":", gFile );
		JsonString( gFile, VR_BUILD_COMMIT_SHORT );
		fprintf( gFile, ",\"build_dirty\":%s", VR_BUILD_DIRTY ? "true" : "false" );
		fputs( ",\"build_source_fingerprint\":", gFile );
		JsonString( gFile, VR_BUILD_SOURCE_FINGERPRINT );
		fputs( ",\"build_generated_at\":", gFile );
		JsonString( gFile, VR_BUILD_GENERATED_AT );
		fputs( ",\"build_configuration\":", gFile );
		JsonString( gFile, VR_BUILD_CONFIGURATION );
		fputs( ",\"build_platform\":", gFile );
		JsonString( gFile, VR_BUILD_PLATFORM );
		fputs( ",\"build_target\":", gFile );
		JsonString( gFile, VR_BUILD_TARGET );
		const char* recentChanges[8] = {
			VR_BUILD_RECENT_CHANGE_1, VR_BUILD_RECENT_CHANGE_2,
			VR_BUILD_RECENT_CHANGE_3, VR_BUILD_RECENT_CHANGE_4,
			VR_BUILD_RECENT_CHANGE_5, VR_BUILD_RECENT_CHANGE_6,
			VR_BUILD_RECENT_CHANGE_7, VR_BUILD_RECENT_CHANGE_8 };
		fputs( ",\"recent_changes\":[", gFile );
		for( int i = 0; i < VR_BUILD_RECENT_CHANGE_COUNT && i < 8; ++i )
		{
			if( i ) fputc( ',', gFile );
			JsonString( gFile, recentChanges[i] );
		}
		fputc( ']', gFile );
		fputs( ",\"blackbox_version\":3,\"decision_forensics\":true,\"build_provenance_version\":1", gFile );
		fputs( "}\n", gFile );
		fflush( gFile );
	}

	FILE* BeginEvent( VRAnalyticsLayer layer, const char* kind, unsigned long decisionId )
	{
		Initialize();
		if( !gEnabled || !gFile )
			return NULL;

		++gSequence;
		fprintf( gFile,
			"{\"schema\":\"vr-blackbox-1\",\"seq\":%lu,\"session\":%lu,"
			"\"layer\":",
			gSequence, gSessionId );
		JsonString( gFile, LayerName( layer ) );
		fputs( ",\"kind\":", gFile );
		JsonString( gFile, kind );
		if( decisionId )
			fprintf( gFile, ",\"decision_id\":%lu", decisionId );
		if( layer == VR_ANALYTICS_TACTICAL && gActiveBattleId )
			fprintf( gFile, ",\"battle_id\":%lu", gActiveBattleId );
		return gFile;
	}

	void EndEvent( FILE* file )
	{
		if( !file )
			return;
		fputs( "}\n", file );
		fflush( file );
	}

	unsigned long EnsureTacticalDecision( unsigned int soldierId )
	{
		TacticalDecisionTrace* trace;

		if( soldierId >= 256 )
			return VRAnalyticsBeginDecision(
				VR_ANALYTICS_TACTICAL, "soldier", soldierId, "tactical_evaluation" );

		trace = &gTacticalTrace[soldierId];
		if( !trace->decisionId )
		{
			trace->decisionId = VRAnalyticsBeginDecision(
				VR_ANALYTICS_TACTICAL, "soldier", soldierId, "tactical_evaluation" );
			trace->selected = false;
		}
		return trace->decisionId;
	}
}

void VRAnalyticsSetEnabled( bool enabled )
{
	gEnabled = enabled;
}

bool VRAnalyticsIsEnabled()
{
	Initialize();
	return gEnabled;
}

unsigned long VRAnalyticsBeginDecision(
	VRAnalyticsLayer layer,
	const char* actorType,
	unsigned int actorId,
	const char* decisionType )
{
	FILE* file;
	unsigned long decisionId;

	Initialize();
	if( !gEnabled )
		return 0;

	decisionId = ++gDecisionSequence;
	gDecisionMeta[ decisionId % DECISION_META_CAPACITY ].id = decisionId;
	gDecisionMeta[ decisionId % DECISION_META_CAPACITY ].layer = layer;
	file = BeginEvent( layer, "decision_begin", decisionId );
	if( !file )
		return 0;

	fputs( ",\"actor_type\":", file );
	JsonString( file, actorType );
	fprintf( file, ",\"actor_id\":%u,\"decision_type\":", actorId );
	JsonString( file, decisionType );
	EndEvent( file );
	return decisionId;
}

void VRAnalyticsStateInt(
	unsigned long decisionId,
	const char* key,
	long value )
{
	FILE* file = BeginEvent( DecisionLayer( decisionId ), "state", decisionId );
	if( !file )
		return;

	fputs( ",\"key\":", file );
	JsonString( file, key );
	fprintf( file, ",\"value\":%ld", value );
	EndEvent( file );
}

void VRAnalyticsCandidate(
	unsigned long decisionId,
	const char* candidate,
	long subjectId,
	long rawScore,
	long adjustedScore,
	bool eligible,
	const char* reason )
{
	FILE* file;

	// Generic candidate events are layer-neutral at the API boundary. The
	// layer is recoverable from decision_begin; use strategic here only for
	// display grouping when no tactical wrapper is involved.
	file = BeginEvent( DecisionLayer( decisionId ), "candidate", decisionId );
	if( !file )
		return;

	fputs( ",\"candidate\":", file );
	JsonString( file, candidate );
	fprintf( file,
		",\"subject_id\":%ld,\"raw_score\":%ld,\"adjusted_score\":%ld,\"eligible\":%s,\"reason\":",
		subjectId, rawScore, adjustedScore, eligible ? "true" : "false" );
	JsonString( file, reason );
	EndEvent( file );
}

void VRAnalyticsCommitDecision(
	unsigned long decisionId,
	const char* selection,
	long subjectId,
	long score,
	const char* reason )
{
	FILE* file = BeginEvent( DecisionLayer( decisionId ), "decision_commit", decisionId );
	if( !file )
		return;

	fputs( ",\"selection\":", file );
	JsonString( file, selection );
	fprintf( file, ",\"subject_id\":%ld,\"score\":%ld,\"reason\":",
		subjectId, score );
	JsonString( file, reason );
	EndEvent( file );
}

void VRAnalyticsOutcome(
	unsigned long decisionId,
	const char* status,
	const char* metricA,
	long valueA,
	const char* metricB,
	long valueB,
	const char* detail )
{
	FILE* file = BeginEvent( DecisionLayer( decisionId ), "outcome", decisionId );
	if( !file )
		return;

	fputs( ",\"status\":", file );
	JsonString( file, status );
	fputs( ",\"metric_a\":", file );
	JsonString( file, metricA );
	fprintf( file, ",\"value_a\":%ld,\"metric_b\":", valueA );
	JsonString( file, metricB );
	fprintf( file, ",\"value_b\":%ld,\"detail\":", valueB );
	JsonString( file, detail );
	EndEvent( file );
}

void VRAnalyticsDiagnostic(
	VRAnalyticsLayer layer,
	const char* actorType,
	unsigned int actorId,
	const char* code,
	const char* detail )
{
	FILE* file = BeginEvent( layer, "diagnostic", 0 );
	if( !file )
		return;

	fputs( ",\"actor_type\":", file );
	JsonString( file, actorType );
	fprintf( file, ",\"actor_id\":%u,\"code\":", actorId );
	JsonString( file, code );
	fputs( ",\"detail\":", file );
	JsonString( file, detail );
	EndEvent( file );
}

void VRAnalyticsBattleStarted(
	unsigned long worldMinutes,
	int sectorX,
	int sectorY,
	int sectorZ,
	int playerCount,
	int enemyCount,
	int militiaCount )
{
	FILE* file;

	Initialize();
	if( !gEnabled )
		return;

	if( gActiveBattleId )
	{
		if( gBattleSectorX == sectorX &&
			gBattleSectorY == sectorY &&
			gBattleSectorZ == sectorZ )
		{
			// Combat mode may temporarily end and resume while the same tactical
			// battle is still unresolved. Keep the existing causal battle ID.
			return;
		}

		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "battle", (unsigned int)gActiveBattleId,
			"battle_replaced_without_end",
			"new sector entered while previous battle telemetry remained open" );
	}

	gActiveBattleId = ++gBattleSequence;
	gBattleSectorX = sectorX;
	gBattleSectorY = sectorY;
	gBattleSectorZ = sectorZ;
	gBattleStartPlayers = playerCount;
	gBattleStartEnemies = enemyCount;
	gBattleStartMilitia = militiaCount;

	file = BeginEvent( VR_ANALYTICS_TACTICAL, "battle_start", 0 );
	if( !file )
		return;

	fprintf( file,
		",\"battle_id\":%lu,\"world_minutes\":%lu,\"sector_x\":%d,\"sector_y\":%d,\"sector_z\":%d,"
		"\"player_count\":%d,\"enemy_count\":%d,\"militia_count\":%d",
		gActiveBattleId, worldMinutes, sectorX, sectorY, sectorZ,
		playerCount, enemyCount, militiaCount );
	EndEvent( file );
}

void VRAnalyticsBattleEnded(
	const char* result,
	unsigned long worldMinutes,
	int sectorX,
	int sectorY,
	int sectorZ,
	int playerCount,
	int enemyCount,
	int militiaCount,
	bool enemyRetreated )
{
	FILE* file;

	Initialize();
	if( !gEnabled )
		return;

	if( !gActiveBattleId )
	{
		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "battle", 0,
			"battle_end_without_start",
			"battle ended without a matching telemetry start event" );
		return;
	}

	file = BeginEvent( VR_ANALYTICS_TACTICAL, "battle_end", 0 );
	if( file )
	{
		fprintf( file,
			",\"battle_id\":%lu,\"result\":",
			gActiveBattleId );
		JsonString( file, result );
		fprintf( file,
			",\"world_minutes\":%lu,\"sector_x\":%d,\"sector_y\":%d,\"sector_z\":%d,"
			"\"start_player_count\":%d,\"end_player_count\":%d,"
			"\"start_enemy_count\":%d,\"end_enemy_count\":%d,"
			"\"start_militia_count\":%d,\"end_militia_count\":%d,"
			"\"player_count_delta\":%d,\"enemy_count_delta\":%d,"
			"\"militia_count_delta\":%d,\"enemy_retreated\":%s",
			worldMinutes, sectorX, sectorY, sectorZ,
			gBattleStartPlayers, playerCount,
			gBattleStartEnemies, enemyCount,
			gBattleStartMilitia, militiaCount,
			playerCount - gBattleStartPlayers,
			enemyCount - gBattleStartEnemies,
			militiaCount - gBattleStartMilitia,
			enemyRetreated ? "true" : "false" );
		EndEvent( file );
	}

	gActiveBattleId = 0;
	gBattleSectorX = gBattleSectorY = gBattleSectorZ = 0;
	gBattleStartPlayers = gBattleStartEnemies = gBattleStartMilitia = 0;
}

void VRAnalyticsStrategicMoveOrdered(
	unsigned long worldMinutes,
	unsigned int groupId,
	int sourceSector,
	int targetSector,
	int groupSize,
	int moveCode,
	int intention )
{
	FILE* file = BeginEvent( VR_ANALYTICS_STRATEGIC, "strategic_move_order", 0 );
	if( !file )
		return;

	fprintf( file,
		",\"world_minutes\":%lu,\"group_id\":%u,\"source_sector\":%d,"
		"\"target_sector\":%d,\"group_size\":%d,\"move_code\":%d,\"intention\":%d",
		worldMinutes, groupId, sourceSector, targetSector, groupSize, moveCode, intention );
	EndEvent( file );
}

void VRAnalyticsStrategicGroupArrived(
	unsigned long worldMinutes,
	unsigned int groupId,
	int sector,
	int groupSize,
	const char* assignment )
{
	FILE* file = BeginEvent( VR_ANALYTICS_STRATEGIC, "strategic_group_arrived", 0 );
	if( !file )
		return;

	fprintf( file,
		",\"world_minutes\":%lu,\"group_id\":%u,\"sector\":%d,\"group_size\":%d,\"assignment\":",
		worldMinutes, groupId, sector, groupSize );
	JsonString( file, assignment );
	EndEvent( file );
}

void VRAnalyticsTacticalCandidate(
	unsigned int soldierId,
	const char* candidate,
	long target,
	long rawScore,
	long adjustedScore,
	bool eligible,
	const char* reason )
{
	unsigned long decisionId = EnsureTacticalDecision( soldierId );
	FILE* file = BeginEvent( VR_ANALYTICS_TACTICAL, "candidate", decisionId );
	if( !file )
		return;

	fputs( ",\"candidate\":", file );
	JsonString( file, candidate );
	fprintf( file,
		",\"subject_id\":%ld,\"raw_score\":%ld,\"adjusted_score\":%ld,\"eligible\":%s,\"reason\":",
		target, rawScore, adjustedScore, eligible ? "true" : "false" );
	JsonString( file, reason );
	EndEvent( file );
}

void VRAnalyticsTacticalStateInt(
	unsigned int soldierId,
	const char* key,
	long value )
{
	unsigned long decisionId = EnsureTacticalDecision( soldierId );
	VRAnalyticsStateInt( decisionId, key, value );
}

void VRAnalyticsTacticalRetreatAssessment(
	unsigned int soldierId,
	unsigned long turn,
	int battleSituation,
	long perceivedFriendlyStrength,
	long perceivedEnemyStrength,
	int knownOpponents,
	int friendlyCasualtyPct,
	int localCasualtyPct,
	int holdConfidence,
	int localStress,
	int personalRisk,
	int riskTolerance,
	int routPressure,
	int collapseStreak,
	bool lastSurvivorPressure,
	int nearbyOperationalFriends,
	bool stableLeaderNearby,
	bool hasCover,
	bool hasSightCover,
	bool underFire,
	int lifePct,
	int marksmanship,
	int experienceLevel,
	int gunDeadliness,
	int gunAmmo,
	bool lastAttackHit,
	bool lastTargetSuppressed,
	bool escapeIntentActive )
{
	unsigned long decisionId = EnsureTacticalDecision( soldierId );
	FILE* file = BeginEvent( VR_ANALYTICS_TACTICAL, "assessment", decisionId );
	if( !file )
		return;

	fprintf( file,
		",\"assessment_type\":\"retreat_courage\",\"actor_id\":%u,\"turn\":%lu,"
		"\"battle_situation\":%d,\"perceived_friendly_strength\":%ld,"
		"\"perceived_enemy_strength\":%ld,\"known_opponents\":%d,"
		"\"friendly_casualty_pct\":%d,\"local_casualty_pct\":%d,"
		"\"hold_confidence\":%d,\"local_stress\":%d,"
		"\"personal_risk\":%d,\"risk_tolerance\":%d,"
		"\"rout_pressure\":%d,\"collapse_streak\":%d,"
		"\"last_survivor_pressure\":%s,\"nearby_operational_friends\":%d,"
		"\"stable_leader_nearby\":%s,\"has_cover\":%s,\"has_sight_cover\":%s,"
		"\"under_fire\":%s,\"life_pct\":%d,\"marksmanship\":%d,"
		"\"experience_level\":%d,\"gun_deadliness\":%d,\"gun_ammo\":%d,"
		"\"last_attack_hit\":%s,\"last_target_suppressed\":%s,"
		"\"escape_intent_active\":%s",
		soldierId, turn, battleSituation,
		perceivedFriendlyStrength, perceivedEnemyStrength, knownOpponents,
		friendlyCasualtyPct, localCasualtyPct, holdConfidence, localStress,
		personalRisk, riskTolerance, routPressure, collapseStreak,
		lastSurvivorPressure ? "true" : "false", nearbyOperationalFriends,
		stableLeaderNearby ? "true" : "false",
		hasCover ? "true" : "false",
		hasSightCover ? "true" : "false",
		underFire ? "true" : "false",
		lifePct, marksmanship, experienceLevel, gunDeadliness, gunAmmo,
		lastAttackHit ? "true" : "false",
		lastTargetSuppressed ? "true" : "false",
		escapeIntentActive ? "true" : "false" );
	EndEvent( file );
}

void VRAnalyticsTacticalFormationSnapshot(
	unsigned long turn,
	int team,
	int sectorX,
	int sectorY,
	int sectorZ,
	int living,
	int combatReady,
	int cowering,
	int disengaging,
	int escaping,
	int leaders,
	int casualtyPercent,
	int averageMorale,
	int averageStress )
{
	FILE* file = BeginEvent( VR_ANALYTICS_TACTICAL, "formation_snapshot", 0 );
	if( !file )
		return;

	fprintf( file,
		",\"turn\":%lu,\"team\":%d,\"sector_x\":%d,\"sector_y\":%d,\"sector_z\":%d,"
		"\"living\":%d,\"combat_ready\":%d,\"cowering\":%d,"
		"\"disengaging\":%d,\"escaping\":%d,\"leaders\":%d,"
		"\"casualty_percent\":%d,\"average_morale\":%d,\"average_stress\":%d",
		turn, team, sectorX, sectorY, sectorZ,
		living, combatReady, cowering, disengaging, escaping, leaders,
		casualtyPercent, averageMorale, averageStress );
	EndEvent( file );
}

void VRAnalyticsTacticalDecisionSelected(
	unsigned int soldierId,
	int team,
	int side,
	bool neutral,
	int profile,
	int soldierClass,
	int action,
	long actionData,
	long gridNo,
	int actionPoints,
	int life,
	int breath,
	int alertStatus,
	int aiMorale,
	int orders,
	int attitude )
{
	unsigned long decisionId;
	TacticalDecisionTrace* trace = NULL;
	FILE* file;

	if( soldierId < 256 )
	{
		trace = &gTacticalTrace[soldierId];

		// A selected action without ActionDone means the previous chain was
		// superseded or aborted. Preserve that fact instead of silently
		// overwriting it.
		if( trace->decisionId && trace->selected )
		{
			VRAnalyticsOutcome(
				trace->decisionId,
				"superseded",
				"end_grid", gridNo,
				"end_ap", actionPoints,
				"new_action_selected_before_previous_action_closed" );
			*trace = TacticalDecisionTrace();
		}

		decisionId = EnsureTacticalDecision( soldierId );
		trace = &gTacticalTrace[soldierId];
		trace->decisionId = decisionId;
		trace->action = action;
		trace->startGrid = gridNo;
		trace->startAP = actionPoints;
		trace->startLife = life;
		trace->startBreath = breath;
		trace->selected = true;
	}
	else
	{
		decisionId = VRAnalyticsBeginDecision(
			VR_ANALYTICS_TACTICAL, "soldier", soldierId, "tactical_action" );
	}

	file = BeginEvent( VR_ANALYTICS_TACTICAL, "decision_commit", decisionId );
	if( !file )
		return;

	fprintf( file,
		",\"actor_id\":%u,\"team\":%d,\"side\":%d,\"neutral\":%s,"
		"\"profile\":%d,\"soldier_class\":%d,\"action\":%d,\"action_data\":%ld,"
		"\"grid\":%ld,\"ap\":%d,\"life\":%d,\"breath\":%d,"
		"\"alert\":%d,\"ai_morale\":%d,\"orders\":%d,\"attitude\":%d,"
		"\"reason\":\"central_ai_selection\"",
		soldierId, team, side, neutral ? "true" : "false", profile, soldierClass,
		action, actionData, gridNo, actionPoints, life, breath,
		alertStatus, aiMorale, orders, attitude );
	EndEvent( file );
}

void VRAnalyticsTacticalActionDone(
	unsigned int soldierId,
	int action,
	long gridNo,
	int actionPoints,
	int life,
	int breath,
	bool lastAttackHit )
{
	TacticalDecisionTrace* trace;
	char detail[256];

	if( soldierId >= 256 )
	{
		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "soldier", soldierId,
			"uncorrelated_action_done", "soldier id outside tactical trace range" );
		return;
	}

	trace = &gTacticalTrace[soldierId];
	if( !trace->decisionId )
	{
		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "soldier", soldierId,
			"uncorrelated_action_done", "ActionDone without an open decision chain" );
		return;
	}

	if( trace->action != action )
	{
		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "soldier", soldierId,
			"action_mismatch", "completed action differs from recorded selected action" );
	}

	const int rawAPDelta = trace->startAP - actionPoints;
	const bool apIncreaseDetected = rawAPDelta < 0;
	const int observedAPSpent = apIncreaseDetected ? -1 : rawAPDelta;
	sprintf( detail,
		"life_delta=%d;breath_delta=%d;last_attack_hit=%d;action=%d;"
		"ap_before=%d;ap_after=%d;ap_increase_detected=%d",
		life - trace->startLife,
		breath - trace->startBreath,
		lastAttackHit ? 1 : 0,
		action, trace->startAP, actionPoints,
		apIncreaseDetected ? 1 : 0 );
	VRAnalyticsOutcome(
		trace->decisionId,
		"completed",
		"grid_delta", gridNo - trace->startGrid,
		"ap_spent", observedAPSpent,
		detail );

	*trace = TacticalDecisionTrace();
}

void VRAnalyticsTacticalActionRejected(
	unsigned int soldierId,
	int action,
	long actionData,
	const char* reason )
{
	TacticalDecisionTrace* trace;
	char detail[128];

	if( soldierId >= 256 )
	{
		VRAnalyticsDiagnostic(
			VR_ANALYTICS_TACTICAL, "soldier", soldierId,
			"action_rejected", reason );
		return;
	}

	trace = &gTacticalTrace[soldierId];
	if( !trace->decisionId )
		trace->decisionId = EnsureTacticalDecision( soldierId );

	sprintf( detail, "action=%d;action_data=%ld", action, actionData );
	VRAnalyticsOutcome(
		trace->decisionId,
		"rejected",
		"action", action,
		"action_data", actionData,
		reason ? reason : detail );

	*trace = TacticalDecisionTrace();
}
