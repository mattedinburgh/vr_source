#include "VRAnalytics.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

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

		++gSequence;
		fprintf( gFile,
			"{\"schema\":\"vr-blackbox-1\",\"seq\":%lu,\"session\":%lu,"
			"\"layer\":\"system\",\"kind\":\"session_start\","
			"\"build_date\":",
			gSequence, gSessionId );
		JsonString( gFile, __DATE__ );
		fputs( ",\"build_time\":", gFile );
		JsonString( gFile, __TIME__ );
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

void VRAnalyticsTacticalDecisionSelected(
	unsigned int soldierId,
	int team,
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
		",\"actor_id\":%u,\"team\":%d,\"action\":%d,\"action_data\":%ld,"
		"\"grid\":%ld,\"ap\":%d,\"life\":%d,\"breath\":%d,"
		"\"alert\":%d,\"ai_morale\":%d,\"orders\":%d,\"attitude\":%d,"
		"\"reason\":\"central_ai_selection\"",
		soldierId, team, action, actionData, gridNo, actionPoints, life, breath,
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
	char detail[160];

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

	sprintf( detail,
		"life_delta=%d;breath_delta=%d;last_attack_hit=%d;action=%d",
		life - trace->startLife,
		breath - trace->startBreath,
		lastAttackHit ? 1 : 0,
		action );

	VRAnalyticsOutcome(
		trace->decisionId,
		"completed",
		"grid_delta", gridNo - trace->startGrid,
		"ap_spent", trace->startAP - actionPoints,
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
