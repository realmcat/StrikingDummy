#include "BlackMage.h"
#include "Logger.h"
#include <assert.h>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <random>
#include <iostream>
#include <Eigen/Core>
#include <fstream>
using namespace Eigen;

#ifdef _DEBUG 
#define DBG(x) x
#else 
#define DBG(x)
#endif

namespace StrikingDummy
{
	BlackMage::BlackMage(Stats& stats) : 
		Job(stats, BLM_ATTR),
		base_gcd(lround(floor(0.1f * floor(this->stats.ss_multiplier * BASE_GCD)))),
		iii_gcd(lround(floor(0.1f * floor(this->stats.ss_multiplier * III_GCD)))),
		iv_gcd(lround(floor(0.1f * floor(this->stats.ss_multiplier * IV_GCD)))),
		despair_gcd(lround(floor(0.1f * floor(this->stats.ss_multiplier * DESPAIR_GCD)))),
		fast_base_gcd(lround(floor(0.1f * floor(0.5f * floor(this->stats.ss_multiplier * BASE_GCD))))),
		fast_iii_gcd(lround(floor(0.1f * floor(0.5f * floor(this->stats.ss_multiplier * III_GCD))))),
		fast_despair_gcd(lround(floor(0.1f * floor(0.5f * floor(this->stats.ss_multiplier * DESPAIR_GCD))))),
		ll_base_gcd(lround(floor(0.1f * floor(0.85 * floor(this->stats.ss_multiplier * BASE_GCD))))),
		ll_iii_gcd(lround(floor(0.1f * floor(0.85 * floor(this->stats.ss_multiplier * III_GCD))))),
		ll_iv_gcd(lround(floor(0.1f * floor(0.85 * floor(this->stats.ss_multiplier * IV_GCD))))),
		ll_despair_gcd(lround(floor(0.1f * floor(0.85 * floor(this->stats.ss_multiplier * DESPAIR_GCD))))),
		ll_fast_base_gcd(lround(floor(0.1f * floor(0.5f * floor(0.85 * floor(this->stats.ss_multiplier * BASE_GCD)))))),
		ll_fast_iii_gcd(lround(floor(0.1f * floor(0.5f * floor(0.85 * floor(this->stats.ss_multiplier * III_GCD)))))),
		ll_fast_despair_gcd(lround(floor(0.1f * floor(0.5f * floor(0.85 * floor(this->stats.ss_multiplier * DESPAIR_GCD))))))
	{
		actions.reserve(NUM_ACTIONS);
		reset();
	}

	void BlackMage::reset()
	{
		timeline = {};

		//mp = MAX_MP;
		mp = MAX_MP - B3_MP_COST;

		//element = Element::NE;
		element = Element::UI;
		umbral_hearts = 0;
		enochian = false;

		// server ticks
		mp_timer.reset(tick(rng), false);
		dot_timer.reset(tick(rng), false);
		timeline.push_event(mp_timer.time);
		timeline.push_event(dot_timer.time);

		// elemental gauge
		gauge.reset(0, 0);
		xeno_timer.reset(0, false);
		xeno_procs = 0;

		// buffs
		swift.reset(0, 0);
		sharp.reset(0, 0);

		triple.reset(0, 0);
		leylines.reset(0, 0);
		fs_proc.reset(0, 0);
		tc_proc.reset(0, 0);
		dot.reset(0, 0);

		// cooldowns		
		swift_cd.reset(0, true);
		triple_cd.reset(0, true);
		sharp_cd.reset(0, true);
		leylines_cd.reset(0, true);
		manafont_cd.reset(0, true);
		eno_cd.reset(0, true);
		transpose_cd.reset(0, true);

		// actions
		gcd_timer.reset(0, true);
		cast_timer.reset(0, false);
		action_timer.reset(0, true);
		casting = Action::NONE;
		casting_mp_cost = 0;

		// metrics
		total_damage = 0;
		xeno_count = 0;
		f4_count = 0;
		b4_count = 0;
		t3_count = 0;
		despair_count = 0;
		transpose_count = 0;

		// precast
		gauge.reset(GAUGE_DURATION, 3);
		sharp.reset(SHARP_DURATION - 1000, 1);
		sharp_cd.reset(SHARP_CD - 1000, false);
		timeline.push_event(gauge.time);
		timeline.push_event(sharp.time);
		timeline.push_event(sharp_cd.time);
		
		history.clear();

		update_history();
	}

	void BlackMage::update(int elapsed)
	{
		DBG(assert(elapsed > 0));

		// server ticks
		mp_timer.update(elapsed);
		dot_timer.update(elapsed);

		// elemental gauge
		gauge.update(elapsed);
		xeno_timer.update(elapsed);

		// buffs
		swift.update(elapsed);
		sharp.update(elapsed);
		triple.update(elapsed);
		leylines.update(elapsed);
		fs_proc.update(elapsed);
		tc_proc.update(elapsed);
		dot.update(elapsed);

		// cooldowns
		swift_cd.update(elapsed);
		triple_cd.update(elapsed);
		sharp_cd.update(elapsed);
		leylines_cd.update(elapsed);
		manafont_cd.update(elapsed);
		eno_cd.update(elapsed);
		transpose_cd.update(elapsed);

		// actions
		gcd_timer.update(elapsed);
		cast_timer.update(elapsed);
		action_timer.update(elapsed);

		// 
		if (mp_timer.ready)
			update_mp();
		if (dot_timer.ready)
			update_dot();
		if (element != Element::NE && gauge.count == 0)
		{
			element = Element::NE;
			umbral_hearts = 0;
			enochian = false;
			xeno_timer.time = 0;
		}
		if (enochian && xeno_timer.time == 0)
		{
			xeno_timer.time = XENO_TIMER;
			assert(xeno_timer.ready);
			xeno_procs = std::min(xeno_procs + 1, 2);
			xeno_timer.ready = false;
			push_event(XENO_TIMER);
		}
		if (cast_timer.ready)
			end_action();
		
		update_history();
	}

	void BlackMage::update_history()
	{
		actions.clear();
		if (action_timer.ready)
		{
			for (int i = 0; i < NUM_ACTIONS; i++) if (can_use_action(i)) actions.push_back(i);
			
			if (actions.empty() || (actions.size() == 1 && actions[0] == NONE))
				return;

			// state/transition
			if (!history.empty())
			{
				Transition& t = history.back();
				get_state(t.t1);
				t.dt = timeline.time - t.dt;
				t.actions = actions;
			}

			history.emplace_back();
			Transition& t = history.back();
			get_state(t.t0);
			t.reward = 0.0f;
			t.dt = timeline.time;
		}
	}

	void BlackMage::update_mp()
	{
		if (element != Element::AF)
		{
			switch (gauge.count)
			{
			case 0:
				mp += MP_PER_TICK;
				break;
			case 1:
				mp += MP_PER_TICK_UI1;
				break;
			case 2:
				mp += MP_PER_TICK_UI2;
				break;
			case 3:
				mp += MP_PER_TICK_UI3;
			}
			if (mp > MAX_MP)
				mp = MAX_MP;
		}
		mp_timer.reset(TICK_TIMER, false);
		push_event(TICK_TIMER);
	}

	void BlackMage::update_dot()
	{
		if (dot.count > 0)
		{
			float damage = get_dot_damage();
			total_damage += damage;
			history.back().reward += damage;
			if (prob(rng) < TC_PROC_RATE)
			{
				tc_proc.reset(TC_DURATION, 1);
				push_event(TC_DURATION);
			}
		}
		dot_timer.reset(TICK_TIMER, false);
		push_event(TICK_TIMER);
	}

	bool BlackMage::is_instant_cast(int action) const
	{
		// for gcds
		return swift.count == 1 || triple.count > 0 || (action == F3 && fs_proc.count > 0) || (action == T3 && tc_proc.count > 0) || action == XENO ||action == UMBRAL_SOUL;
	}

	int BlackMage::get_ll_cast_time(int ll_cast_time, int cast_time) const
	{
		return (leylines.count == 1 && ll_cast_time < leylines.time) ? ll_cast_time : cast_time;
	}

	int BlackMage::get_cast_time(int action) const
	{
		// for gcds
		if (is_instant_cast(action))
			return 0;

		switch (action)
		{
		case B1:
			if (element == AF && gauge.count == 3)
				return get_ll_cast_time(ll_fast_base_gcd, fast_base_gcd);
			return get_ll_cast_time(ll_base_gcd, base_gcd);
		case B3:
			if (element == AF && gauge.count == 3)
				return get_ll_cast_time(ll_fast_iii_gcd, fast_iii_gcd);
			return get_ll_cast_time(ll_iii_gcd, iii_gcd);
		case B4:
			return get_ll_cast_time(ll_iv_gcd, iv_gcd);
		case FREEZE:
			if (element == AF && gauge.count == 3)
				return get_ll_cast_time(ll_fast_despair_gcd, fast_despair_gcd);
			return get_ll_cast_time(ll_despair_gcd, despair_gcd);
		case F1:
			if (element == UI && gauge.count == 3)
				return get_ll_cast_time(ll_fast_base_gcd, fast_base_gcd);
			return get_ll_cast_time(ll_base_gcd, base_gcd);
		case F3:
			if (element == UI && gauge.count == 3)
				return get_ll_cast_time(ll_fast_iii_gcd, fast_iii_gcd);
			return get_ll_cast_time(ll_iii_gcd, iii_gcd);
		case F4:
			return get_ll_cast_time(ll_iv_gcd, iv_gcd);
		case T3:
		case XENO:
		case UMBRAL_SOUL:
			return get_ll_cast_time(ll_base_gcd, base_gcd);
		case DESPAIR:
			return get_ll_cast_time(ll_despair_gcd, despair_gcd);
		}
		return 99999;
	}

	int BlackMage::get_action_time(int action) const
	{
		if (is_instant_cast(action))
			return ANIMATION_LOCK + ACTION_TAX;
		return get_cast_time(action) + ACTION_TAX;
	}

	int BlackMage::get_gcd_time(int action) const
	{
		if (is_instant_cast(action))
			return leylines.count > 0 ? ll_base_gcd : base_gcd;
		return get_ll_cast_time(ll_base_gcd, base_gcd);
	}

	bool BlackMage::can_use_action(int action) const
	{
		// only checked if actions aren't locked
		switch (action)
		{
		case NONE:
			return !gcd_timer.ready;
		case B1:
			return gcd_timer.ready && get_mp_cost(B1) <= mp;
		case B3:
			return gcd_timer.ready && get_mp_cost(B3) <= mp;
		case B4:
			return gcd_timer.ready && element == UI && enochian && get_cast_time(B4) < gauge.time && get_mp_cost(B4) <= mp;
		case FREEZE:
			return false;
			//return gcd_timer.ready && get_mp_cost(FREEZE) <= mp;
		case F1:
			return gcd_timer.ready && get_mp_cost(F1) <= mp;
		case F3:
			return gcd_timer.ready && get_mp_cost(F3) <= mp;
		case F4:
			return gcd_timer.ready && element == AF && enochian && get_cast_time(F4) < gauge.time && get_mp_cost(F4) <= mp;
		case T3:
			return gcd_timer.ready && get_mp_cost(T3) <= mp;
		case XENO:
			return gcd_timer.ready && xeno_procs > 0;
		case DESPAIR:
			return gcd_timer.ready && element == AF && enochian && get_mp_cost(DESPAIR) <= mp;
		case UMBRAL_SOUL:
			return false;
			//return gcd_timer.ready && element == UI && enochian && gauge.count < 3;
		case SWIFT:
			return swift_cd.ready;
		case TRIPLE:
			return triple_cd.ready;
		case SHARP:
			return sharp_cd.ready;
		case LEYLINES:
			return leylines_cd.ready;
		case MANAFONT:
			return manafont_cd.ready;
		case ENOCHIAN:
			return !enochian && eno_cd.ready && element != Element::NE;
		case TRANSPOSE:
			return transpose_cd.ready && element != Element::NE;
		case WAIT_FOR_MP:
			return gcd_timer.ready && element != Element::AF;
		}
		return false;
	}

	void BlackMage::use_action(int action)
	{
		history.back().action = action;
		switch (action)
		{
		case NONE:
			return;
		case WAIT_FOR_MP:
			action_timer.reset(mp_timer.time, false);
			push_event(action_timer.time);
			return;
		case B1:
		case B3:
		case B4:
		case FREEZE:
		case F1:
		case F3:
		case F4:
		case T3:
		case XENO:
		case DESPAIR:
		case UMBRAL_SOUL:
			if (action == XENO)
				xeno_count++;
			else if (action == F4)
				f4_count++;
			else if (action == B4)
				b4_count++;
			else if (action == T3)
				t3_count++;
			else if (action == DESPAIR)
				despair_count++;
			gcd_timer.reset(get_gcd_time(action), false);
			cast_timer.reset(get_cast_time(action), false);
			action_timer.reset(get_action_time(action), false);
			casting = action;
			casting_mp_cost = get_mp_cost(action);
			assert(casting_mp_cost <= mp);
			if (cast_timer.time == 0)
				end_action();
			push_event(gcd_timer.time);
			push_event(cast_timer.time);
			push_event(action_timer.time);
			return;
		case SWIFT:
			swift.reset(SWIFT_DURATION, 1);
			swift_cd.reset(SWIFT_CD, false);
			push_event(SWIFT_DURATION);
			push_event(SWIFT_CD);
			break;
		case TRIPLE:
			triple.reset(TRIPLE_DURATION, 3);
			triple_cd.reset(TRIPLE_CD, false);
			push_event(TRIPLE_DURATION);
			push_event(TRIPLE_CD);
			break;
		case SHARP:
			sharp.reset(SHARP_DURATION, 1);
			sharp_cd.reset(SHARP_CD, false);
			push_event(SHARP_DURATION);
			push_event(SHARP_CD);
			break;
		case LEYLINES:
			leylines.reset(LL_DURATION, 1);
			leylines_cd.reset(LL_CD, false);
			push_event(LL_DURATION);
			push_event(LL_CD);
			break;
		case MANAFONT:
			mp = std::min(mp + MANAFONT_MP, MAX_MP);
			manafont_cd.reset(MANAFONT_CD, false);
			push_event(MANAFONT_CD);
			break;
		case ENOCHIAN:
			if (!enochian)
			{
				xeno_timer.time = XENO_TIMER;
				push_event(XENO_TIMER);
			}
			enochian = true;
			eno_cd.reset(ENO_CD, false);
			push_event(ENO_CD);
			break;
		case TRANSPOSE:
			assert(element != Element::NE);
			element = element == Element::AF ? Element::UI : Element::AF;
			transpose_cd.reset(TRANSPOSE_CD, false);
			gauge.reset(GAUGE_DURATION, 1);
			push_event(TRANSPOSE_CD);
			push_event(GAUGE_DURATION);
			transpose_count++;
		}
		// ogcd only
		action_timer.reset(ANIMATION_LOCK + ACTION_TAX, false);
		push_event(action_timer.time);
	}

	void BlackMage::end_action()
	{
		assert(casting != NONE);
		assert(cast_timer.time == 0);
		assert(cast_timer.ready || is_instant_cast(casting));
		assert(mp >= casting_mp_cost);

		if (casting == DESPAIR)
			mp = 0;
			//mp = umbral_hearts > 0 ? mp / 3.0f : 0;
		else
			mp -= casting_mp_cost;

		float damage = get_damage(casting);
		total_damage += damage;
		history.back().reward += damage;

		if (casting == F3 && fs_proc.count > 0);
		// firestarter doesn't use swift or triple
		else if (casting == T3 && tc_proc.count > 0);
		// thundercloud doesn't use swift or triple
		else if (casting == UMBRAL_SOUL);
		// umbral dsoul doesn't use swift or triple
		else if (swift.count > 0)
			swift.reset(0, 0);
		else if (triple.count > 1)
			triple.count--;
		else if (triple.count == 1)
			triple.reset(0, 0);

		switch (casting)
		{
		case B1:
			if (element == AF)
			{
				element = Element::NE;
				umbral_hearts = 0;
				enochian = false;
				gauge.reset(0, 0);
				xeno_timer.time = 0;
			}
			else
			{
				element = Element::UI;
				gauge.reset(GAUGE_DURATION, std::min(gauge.count + 1, 3));
				push_event(GAUGE_DURATION);
			}
			break;
		case B3:
			element = UI;
			gauge.reset(GAUGE_DURATION, 3);
			push_event(GAUGE_DURATION);
			break;
		case B4:
			umbral_hearts = 3;
			break;
		case FREEZE:
			element = UI;
			gauge.reset(GAUGE_DURATION, 3);
			push_event(GAUGE_DURATION);
			umbral_hearts = std::min(umbral_hearts + 1, 3);
			break;
		case F1:
			if (element == UI)
			{
				element = Element::NE;
				umbral_hearts = 0;
				enochian = false;
				gauge.reset(0, 0);
				xeno_timer.time = 0;
			}
			else
			{
				element = Element::AF;
				gauge.reset(GAUGE_DURATION, std::min(gauge.count + 1, 3));
				push_event(GAUGE_DURATION);
				if (umbral_hearts > 0)
					umbral_hearts--;
			}
			if (sharp.count > 0 || prob(rng) < FS_PROC_RATE)
			{
				fs_proc.reset(FS_DURATION, 1);
				sharp.reset(0, 0);
				push_event(FS_DURATION);
			}
			break;
		case F3:
			if (fs_proc.count > 0)
				fs_proc.reset(0, 0);
			else if (element == AF && umbral_hearts > 0)
				umbral_hearts--;
			element = AF;
			gauge.reset(GAUGE_DURATION, 3);
			push_event(GAUGE_DURATION);
			break;
		case F4:
			if (umbral_hearts > 0)
				umbral_hearts--;
			break;
		case T3:
			if (tc_proc.count > 0)
				tc_proc.reset(0, 0);
			dot.reset(DOT_DURATION, enochian ? 1 : 2);
			push_event(DOT_DURATION);
			if (sharp.count > 0)
			{
				tc_proc.reset(TC_DURATION, 1);
				sharp.reset(0, 0);
				push_event(TC_DURATION);
			}
			break;
		case XENO:
			assert(xeno_procs > 0);
			xeno_procs--;
			break;
		case DESPAIR:
			umbral_hearts = 0;
			element = AF;
			gauge.reset(GAUGE_DURATION, 3);
			push_event(GAUGE_DURATION);
			break;
		case UMBRAL_SOUL:
			assert(element == UI);
			umbral_hearts = std::min(umbral_hearts + 1, 3);
			gauge.reset(GAUGE_DURATION, std::min(gauge.count + 1, 3));
			push_event(GAUGE_DURATION);
		}
		casting = NONE;
		cast_timer.ready = false;
	}

	int BlackMage::get_mp_cost(int action) const
	{
		switch (action)
		{
		case B1:
			if (element == AF && get_cast_time(B1) < gauge.time)
			{
				if (gauge.count == 1)
					return B1_MP_COST / 2;
				else if (gauge.count == 2)
					return B1_MP_COST / 4;
				return 0;
			}
			else if (element == UI && get_cast_time(B1) < gauge.time)
			{
				if (gauge.count == 1)
					return B1_MP_COST * 3 / 4;
				else if (gauge.count == 2)
					return B1_MP_COST / 2;
				return 0;
			}
			return B1_MP_COST;
		case B3:
			if (element == AF && get_cast_time(B3) < gauge.time)
			{
				if (gauge.count == 1)
					return B3_MP_COST / 2;
				else if (gauge.count == 2)
					return B3_MP_COST / 4;
				return 0;
			}
			else if (element == UI && get_cast_time(B3) < gauge.time)
			{
				if (gauge.count == 1)
					return B3_MP_COST * 3 / 4;
				else if (gauge.count == 2)
					return B3_MP_COST / 2;
				return 0;
			}
			return B3_MP_COST;
		case B4:
			if (element == UI && get_cast_time(B4) < gauge.time)
			{
				if (gauge.count == 1)
					return B4_MP_COST * 3 / 4;
				else if (gauge.count == 2)
					return B4_MP_COST / 2;
				return 0;
			}
			return B4_MP_COST;
		case FREEZE:
			if (element == AF && get_cast_time(FREEZE) < gauge.time)
			{
				if (gauge.count == 1)
					return FREEZE_MP_COST / 2;
				else if (gauge.count == 2)
					return FREEZE_MP_COST / 4;
				return 0;
			}
			else if (element == UI && get_cast_time(FREEZE) < gauge.time)
			{
				if (gauge.count == 1)
					return FREEZE_MP_COST * 3 / 4;
				else if (gauge.count == 2)
					return FREEZE_MP_COST / 2;
				return 0;
			}
			return FREEZE_MP_COST;
		case F1:
			if (element == UI && get_cast_time(F1) < gauge.time)
			{
				if (gauge.count == 1)
					return F1_MP_COST / 2;
				else if (gauge.count == 2)
					return F1_MP_COST / 4;
				return 0;
			}
			return (element == AF && umbral_hearts == 0) ? F1_MP_COST * 2 : F1_MP_COST;
		case F3:
			if (fs_proc.count > 0)
				return 0;
			if (element == UI && get_cast_time(F3) < gauge.time)
			{
				if (gauge.count == 1)
					return F3_MP_COST / 2;
				else if (gauge.count == 2)
					return F3_MP_COST / 4;
				return 0;
			}
			return (element == AF && umbral_hearts == 0) ? F3_MP_COST * 2 : F3_MP_COST;
		case F4:
			return umbral_hearts == 0 ? F4_MP_COST * 2 : F4_MP_COST;
		case T3:
			return tc_proc.count > 0 ? 0 : T3_MP_COST;
		case XENO:
			return 0;
		case DESPAIR:
			return DESPAIR_MP_COST;
		case UMBRAL_SOUL:
			return 0;
		}
		return 99999;
	}

	float BlackMage::get_damage(int action) const
	{
		float potency = 0.0f;
		switch (action)
		{
		case B1:
			if (element == AF)
			{
				if (gauge.count == 1)
					potency = B1_POTENCY * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = B1_POTENCY * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = B1_POTENCY * AF3UI3_MULTIPLIER;
			}
			else
				potency = B1_POTENCY;
			break;
		case B3:
			if (element == AF)
			{
				if (gauge.count == 1)
					potency = B3_POTENCY * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = B3_POTENCY * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = B3_POTENCY * AF3UI3_MULTIPLIER;
			}
			else
				potency = B3_POTENCY;
			break;
		case B4:
			potency = B4_POTENCY;
			break;
		case FREEZE:
			if (element == AF)
			{
				if (gauge.count == 1)
					potency = FREEZE * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = FREEZE * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = FREEZE * AF3UI3_MULTIPLIER;
			}
			else
				potency = FREEZE_POTENCY;
			break;
		case F1:
			if (element == UI)
			{
				if (gauge.count == 1)
					potency = F1_POTENCY * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = F1_POTENCY * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = F1_POTENCY * AF3UI3_MULTIPLIER;
			}
			else if (element == AF)
			{
				if (gauge.count == 1)
					potency = F1_POTENCY * AF1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = F1_POTENCY * AF2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = F1_POTENCY * AF3_MULTIPLIER;
			}
			else
				potency = F1_POTENCY;
			break;
		case F3:
			if (element == UI)
			{
				if (gauge.count == 1)
					potency = F3_POTENCY * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = F3_POTENCY * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = F3_POTENCY * AF3UI3_MULTIPLIER;
			}
			else if (element == AF)
			{
				if (gauge.count == 1)
					potency = F3_POTENCY * AF1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = F3_POTENCY * AF2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = F3_POTENCY * AF3_MULTIPLIER;
			}
			else
				potency = F3_POTENCY;
			break;
		case F4:
			if (element == AF)
			{
				if (gauge.count == 1)
					potency = F4_POTENCY * AF1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = F4_POTENCY * AF2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = F4_POTENCY * AF3_MULTIPLIER;
			}
			break;
		case T3:
			potency = tc_proc.count > 0 ? TC_POTENCY : T3_POTENCY;
			break;
		case XENO:
			potency = XENO_POTENCY;
			break;
		case DESPAIR:
			if (element == UI)
			{
				if (gauge.count == 1)
					potency = DESPAIR_POTENCY * AF1UI1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = DESPAIR_POTENCY * AF2UI2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = DESPAIR_POTENCY * AF3UI3_MULTIPLIER;
			}
			else if (element == AF)
			{
				if (gauge.count == 1)
					potency = DESPAIR_POTENCY * AF1_MULTIPLIER;
				else if (gauge.count == 2)
					potency = DESPAIR_POTENCY * AF2_MULTIPLIER;
				else if (gauge.count == 3)
					potency = DESPAIR_POTENCY * AF3_MULTIPLIER;
			}
			else
				potency = DESPAIR_POTENCY;
			break;
		case UMBRAL_SOUL:
			potency = 0.0f;
		}
		// floor(ptc * wd * ap * det * traits) * chr | * dhr | * rand(.95, 1.05) | ...
		return potency * stats.potency_multiplier * stats.expected_multiplier * (enochian ? ENO_MULTIPLIER : 1.0) * MAGICK_AND_MEND_MULTIPLIER;
	}
	
	float BlackMage::get_dot_damage() const
	{
		// floor(ptc * wd * ap * det * traits) * ss | * rand(.95, 1.05) | * chr | * dhr | ...
		return T3_DOT_POTENCY * stats.potency_multiplier * stats.dot_multiplier * stats.expected_multiplier * (dot.count == 1 ? ENO_MULTIPLIER : 1.0) * MAGICK_AND_MEND_MULTIPLIER;
	}

	void BlackMage::get_state(float* state)
	{
		state[0] = mp / (float)MAX_MP;
		state[1] = element == UI;
		state[2] = element == AF;
		state[3] = umbral_hearts > 0;
		state[4] = enochian;
		state[5] = gauge.count > 0;
		state[6] = gauge.count == 1;
		state[7] = gauge.count == 2;
		state[8] = gauge.count == 3;
		state[9] = gauge.time / (float)GAUGE_DURATION;
		state[10] = xeno_procs > 0;
		state[11] = xeno_procs > 1;
		state[12] = (XENO_TIMER - xeno_timer.time) / (float)XENO_TIMER;
		state[13] = swift.count > 0;
		state[14] = swift.time / (float)SWIFT_DURATION;
		state[15] = sharp.count > 0;
		state[16] = sharp.time / (float)SHARP_DURATION;
		state[17] = triple.count / 3.0f;
		state[18] = triple.time / (float)TRIPLE_DURATION;
		state[19] = leylines.count > 0;
		state[20] = leylines.time / (float)LL_DURATION;
		state[21] = fs_proc.count > 0;
		state[22] = fs_proc.time / (float)FS_DURATION;
		state[23] = tc_proc.count > 0;
		state[24] = tc_proc.time / (float)TC_DURATION;
		state[25] = dot.count > 0;
		state[26] = dot.time / (float)DOT_DURATION;
		state[27] = dot.count == 1;
		state[28] = swift_cd.ready;
		state[29] = swift_cd.time / (float)SWIFT_CD;
		state[30] = triple_cd.ready;
		state[31] = triple_cd.time / (float)TRIPLE_CD;
		state[32] = sharp_cd.ready;
		state[33] = sharp_cd.time / (float)SHARP_CD;
		state[34] = leylines_cd.ready;
		state[35] = leylines_cd.time / (float)LL_CD;
		state[36] = manafont_cd.ready;
		state[37] = manafont_cd.time / (float)MANAFONT_CD;
		state[38] = eno_cd.ready;
		state[39] = eno_cd.time / (float)ENO_CD;
		state[40] = gcd_timer.ready;
		state[41] = gcd_timer.time / 250.0f;
		state[42] = umbral_hearts == 1;
		state[43] = umbral_hearts == 2;
		state[44] = umbral_hearts == 3;
		state[45] = transpose_cd.ready;
		state[46] = transpose_cd.time / (float)TRANSPOSE_CD;
	}

	std::string BlackMage::get_info()
	{
		return "\n";
	}
}