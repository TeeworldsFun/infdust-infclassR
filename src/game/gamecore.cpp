/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include "gamecore.h"

#include "collision.h"

#include <engine/shared/config.h>

const char *CTuningParams::ms_apNames[] =
{
	#define MACRO_TUNING_PARAM(Name, ScriptName, Value, Description) #ScriptName,
	#include "tuning.h"
	#undef MACRO_TUNING_PARAM
};

bool CTuningParams::Set(int Index, float Value)
{
	if(Index < 0 || Index >= Num())
		return false;
	((CTuneParam *)this)[Index] = Value;
	return true;
}

bool CTuningParams::Get(int Index, float *pValue) const
{
	if(Index < 0 || Index >= Num())
		return false;
	*pValue = (float)((CTuneParam *)this)[Index];
	return true;
}

bool CTuningParams::Set(const char *pName, float Value)
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, ms_apNames[i]) == 0)
			return Set(i, Value);
	return false;
}

bool CTuningParams::Get(const char *pName, float *pValue) const
{
	for(int i = 0; i < Num(); i++)
		if(str_comp_nocase(pName, ms_apNames[i]) == 0)
			return Get(i, pValue);

	return false;
}

float HermiteBasis1(float v)
{
	return 2 * v * v * v - 3 * v * v + 1;
}

float VelocityRamp(float Value, float Start, float Range, float Curvature)
{
	if(Value < Start)
		return 1.0f;
	return 1.0f / powf(Curvature, (Value - Start) / Range);
}

const float CCharacterCore::PhysicalSize = 28.0f;
const float CCharacterCore::PassengerYOffset = -50;

void CCharacterCore::Init(CWorldCore *pWorld, CCollision *pCollision)
{
	m_pWorld = pWorld;
	m_pCollision = pCollision;
	Reset();
}

void CCharacterCore::Reset()
{
	m_Pos = vec2(0,0);
	m_Vel = vec2(0,0);
	m_HookPos = vec2(0,0);
	m_HookDir = vec2(0,0);
	m_HookTick = 0;
	m_HookState = HOOK_IDLE;
	m_HookedPlayer = -1;
	m_Jumped = 0;
	m_TriggeredEvents = 0;
	m_Passenger = nullptr;
	m_IsPassenger = false;
	m_ProbablyStucked = false;
}

void CCharacterCore::Tick(bool UseInput, CParams* pParams)
{
	const CTuningParams* pTuningParams = pParams->m_pTuningParams;
	m_TriggeredEvents = 0;

	// get ground state
	bool Grounded = false;
	if(m_pCollision->CheckPoint(m_Pos.x+PhysicalSize/2, m_Pos.y+PhysicalSize/2+5))
		Grounded = true;
	if(m_pCollision->CheckPoint(m_Pos.x-PhysicalSize/2, m_Pos.y+PhysicalSize/2+5))
		Grounded = true;
	
	bool Stucked = false;
	Stucked = m_pCollision->TestBox(m_Pos, vec2(PhysicalSize, PhysicalSize));
	
	vec2 TargetDirection = normalize(vec2(m_Input.m_TargetX, m_Input.m_TargetY));

	m_Vel.y += pTuningParams->m_Gravity;

	float MaxSpeed = Grounded ? pTuningParams->m_GroundControlSpeed : pTuningParams->m_AirControlSpeed;
	float Accel = Grounded ? pTuningParams->m_GroundControlAccel : pTuningParams->m_AirControlAccel;
	float Friction = Grounded ? pTuningParams->m_GroundFriction : pTuningParams->m_AirFriction;

	if (m_ProbablyStucked) {
		m_Pos.y += 1;
		if (!Stucked) {
			m_ProbablyStucked = false;
			m_Pos.y -= 1;
		}
	}
	// InfClassR taxi mode end

	// handle input
	if(UseInput)
	{
		m_Direction = m_Input.m_Direction;

		// setup angle
		float a = 0;
		if(m_Input.m_TargetX == 0)
			a = atanf((float)m_Input.m_TargetY);
		else
			a = atanf((float)m_Input.m_TargetY/(float)m_Input.m_TargetX);

		if(m_Input.m_TargetX < 0)
			a = a+pi;

		m_Angle = (int)(a*256.0f);

		// handle jump
		if(m_Input.m_Jump)
		{
			if(!(m_Jumped&1))
			{
				if(Grounded)
				{
					m_TriggeredEvents |= COREEVENT_GROUND_JUMP;
					m_Vel.y = -pTuningParams->m_GroundJumpImpulse;
					m_Jumped |= 1;
				}
				else if(!(m_Jumped&2))
				{
					m_TriggeredEvents |= COREEVENT_AIR_JUMP;
					m_Vel.y = -pTuningParams->m_AirJumpImpulse;
					m_Jumped |= 3;
				}
			}
		}
		else
			m_Jumped &= ~1;

		// handle hook
		if(m_Input.m_Hook)
		{
			if(m_HookState == HOOK_IDLE)
			{
				m_HookState = HOOK_FLYING;
				m_HookPos = m_Pos+TargetDirection*PhysicalSize*1.5f;
				m_HookDir = TargetDirection;
				m_HookedPlayer = -1;
				m_HookTick = 0;
				m_TriggeredEvents |= COREEVENT_HOOK_LAUNCH;
			}
		}
		else
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_IDLE;
			m_HookPos = m_Pos;
		}
	}

	// add the speed modification according to players wanted direction
	if(m_Direction < 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, -Accel);
	if(m_Direction > 0)
		m_Vel.x = SaturatedAdd(-MaxSpeed, MaxSpeed, m_Vel.x, Accel);
	if(m_Direction == 0)
		m_Vel.x *= Friction;

	// handle jumping
	// 1 bit = to keep track if a jump has been made on this input
	// 2 bit = to keep track if a air-jump has been made
	if(Grounded)
		m_Jumped &= ~2;

	// do hook
	if(m_HookState == HOOK_IDLE)
	{
		m_HookedPlayer = -1;
		m_HookState = HOOK_IDLE;
		m_HookPos = m_Pos;
	}
	else if(m_HookState >= HOOK_RETRACT_START && m_HookState < HOOK_RETRACT_END)
	{
		m_HookState++;
	}
	else if(m_HookState == HOOK_RETRACT_END)
	{
		m_HookState = HOOK_RETRACTED;
		m_TriggeredEvents |= COREEVENT_HOOK_RETRACT;
		m_HookState = HOOK_RETRACTED;
	}
	else if(m_HookState == HOOK_FLYING)
	{
		vec2 NewPos = m_HookPos+m_HookDir*pTuningParams->m_HookFireSpeed;
		if(distance(m_Pos, NewPos) > pTuningParams->m_HookLength)
		{
			m_HookState = HOOK_RETRACT_START;
			NewPos = m_Pos + normalize(NewPos-m_Pos) * pTuningParams->m_HookLength;
		}

		// make sure that the hook doesn't go though the ground
		bool GoingToHitGround = false;
		bool GoingToRetract = false;
		int Hit = m_pCollision->IntersectLine(m_HookPos, NewPos, &NewPos, 0);
		if(Hit)
		{
			if(Hit&CCollision::COLFLAG_NOHOOK)
				GoingToRetract = true;
			else
				GoingToHitGround = true;
		}

		// Check against other players first
		if(m_pWorld)
		{
			float Distance = 0.0f;
			for(int i = 0; i < MAX_CLIENTS; i++)
			{
				CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
				if (IsRecursePassenger(pCharCore))
					continue;
				if(!pCharCore || pCharCore == this || (pCharCore->m_HookProtected && (m_Infected == pCharCore->m_Infected)) || m_IsPassenger || m_Passenger == pCharCore)
					continue;
				if(m_InLove)
					continue;

				vec2 ClosestPoint;
				if(!closest_point_on_line(m_HookPos, NewPos, pCharCore->m_Pos, ClosestPoint))
					continue;

				if(distance(pCharCore->m_Pos, ClosestPoint) < PhysicalSize + 2.0f)
				{
					if(m_HookedPlayer == -1 || distance(m_HookPos, pCharCore->m_Pos) < Distance)
					{
						m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_PLAYER;
						m_HookState = HOOK_GRABBED;
						m_HookedPlayer = i;
						Distance = distance(m_HookPos, pCharCore->m_Pos);
					}
				}
			}
		}

		if(m_HookState == HOOK_FLYING)
		{
			// check against ground
			if(GoingToHitGround)
			{
				m_TriggeredEvents |= COREEVENT_HOOK_ATTACH_GROUND;
				m_HookState = HOOK_GRABBED;
			}
			else if(GoingToRetract)
			{
				m_TriggeredEvents |= COREEVENT_HOOK_HIT_NOHOOK;
				m_HookState = HOOK_RETRACT_START;
			}

			m_HookPos = NewPos;
		}
	}

	if(m_HookState == HOOK_GRABBED)
	{
		if(m_HookedPlayer != -1)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[m_HookedPlayer];
			if(pCharCore)
				m_HookPos = pCharCore->m_Pos;
			else
			{
				// release hook
				m_HookedPlayer = -1;
				m_HookState = HOOK_RETRACTED;
				m_HookPos = m_Pos;
			}
		}

		// don't do this hook routine when we are hook to a player
		if(m_HookedPlayer == -1 && distance(m_HookPos, m_Pos) > 46.0f)
		{
			vec2 HookVel = normalize(m_HookPos-m_Pos)*pTuningParams->m_HookDragAccel;
			// the hook as more power to drag you up then down.
			// this makes it easier to get on top of an platform
			if(HookVel.y > 0)
				HookVel.y *= 0.3f;

			// the hook will boost it's power if the player wants to move
			// in that direction. otherwise it will dampen everything abit
			if((HookVel.x < 0 && m_Direction < 0) || (HookVel.x > 0 && m_Direction > 0))
				HookVel.x *= 0.95f;
			else
				HookVel.x *= 0.75f;

			vec2 NewVel = m_Vel+HookVel;

			// check if we are under the legal limit for the hook
			if(length(NewVel) < pTuningParams->m_HookDragSpeed || length(NewVel) < length(m_Vel))
			{
				m_Vel = NewVel; // no problem. apply
			}
		}

		// release hook (max hook time is 1.25)
		m_HookTick++;
		if(m_HookedPlayer != -1 && (m_HookTick > pParams->m_HookGrabTime || !m_pWorld->m_apCharacters[m_HookedPlayer]))
		{
			m_HookedPlayer = -1;
			m_HookState = HOOK_RETRACTED;
			m_HookPos = m_Pos;
		}
		
		if(pParams->m_HookMode == 1 && distance(m_HookPos, m_Pos) > g_Config.m_InfSpiderWebHookLength)
		{
			// release hook
			m_HookedPlayer = -1;
			m_HookState = HOOK_RETRACTED;
			m_HookPos = m_Pos;
		}
	}

	if(m_pWorld)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			CCharacterCore *pCharCore = m_pWorld->m_apCharacters[i];
			if(!pCharCore)
				continue;

			//player *p = (player*)ent;
			if(pCharCore == this) // || !(p->flags&FLAG_ALIVE)
				continue; // make sure that we don't nudge our self

			float Distance = distance(m_Pos, pCharCore->m_Pos);
			if(Distance > 0)
			{
				vec2 Dir = normalize(m_Pos - pCharCore->m_Pos);

				bool CanCollide = true; // pTuningParams->m_PlayerCollision;
				// handle player <-> player collision
				if((m_Infected == pCharCore->m_Infected) && (m_HookProtected || pCharCore->m_HookProtected))
					CanCollide = false;

				// Totally disable humans body collisions
				if(!m_Infected && !pCharCore->m_Infected)
					CanCollide = false;

				if(CanCollide && Distance < PhysicalSize * 1.25f && Distance > 0.0f)
				{
					float a = (PhysicalSize * 1.45f - Distance);
					float Velocity = 0.5f;

					// make sure that we don't add excess force by checking the
					// direction against the current velocity. if not zero.
					if(length(m_Vel) > 0.0001)
						Velocity = 1 - (dot(normalize(m_Vel), Dir) + 1) / 2;

					m_Vel += Dir * a * (Velocity * 0.75f);
					m_Vel *= 0.85f;
				}

				// handle hook influence
				if(m_HookedPlayer == i)
				{
					if(Distance > PhysicalSize*1.50f) // TODO: fix tweakable variable
					{
						// InfClassR taxi mode, todo: cleanup
						if(g_Config.m_InfTaxi && !pCharCore->m_Passenger && (!m_Infected && !pCharCore->m_Infected && !m_HookProtected) && !IsRecursePassenger(pCharCore)) {
							pCharCore->SetPassenger(this);
							m_HookedPlayer = -1;
							m_HookState = HOOK_RETRACTED;
							m_HookPos = m_Pos;
							continue;
						}
						// InfClassR taxi mode end

						float Accel = pTuningParams->m_HookDragAccel * (Distance/pTuningParams->m_HookLength);
						float DragSpeed = pTuningParams->m_HookDragSpeed;

						// add force to the hooked player
						pCharCore->m_Vel.x = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.x, Accel*Dir.x*1.5f);
						pCharCore->m_Vel.y = SaturatedAdd(-DragSpeed, DragSpeed, pCharCore->m_Vel.y, Accel*Dir.y*1.5f);

						// add a little bit force to the guy who has the grip
						m_Vel.x = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.x, -Accel*Dir.x*0.25f);
						m_Vel.y = SaturatedAdd(-DragSpeed, DragSpeed, m_Vel.y, -Accel*Dir.y*0.25f);
					}
				}
			}
		}
	}

	// clamp the velocity to something sane
	if(length(m_Vel) > 6000)
		m_Vel = normalize(m_Vel) * 6000;

	UpdateTaxiPassengers();
}

void CCharacterCore::Move(CParams* pParams)
{
	const CTuningParams* pTuningParams = pParams->m_pTuningParams;
	
	float RampValue = VelocityRamp(length(m_Vel)*50, pTuningParams->m_VelrampStart, pTuningParams->m_VelrampRange, pTuningParams->m_VelrampCurvature);

	m_Vel.x = m_Vel.x*RampValue;

	vec2 NewPos = m_Pos;
	m_pCollision->MoveBox(&NewPos, &m_Vel, vec2(28.0f, 28.0f), 0);

	m_Vel.x = m_Vel.x*(1.0f/RampValue);

	if(m_pWorld && !pTuningParams->m_PlayerCollision)
	{
		// check player collision
		float Distance = distance(m_Pos, NewPos);
		if(Distance > 0)
		{
			int End = Distance + 1;
			vec2 LastPos = m_Pos;
			for(int i = 0; i < End; i++)
			{
				float a = i / Distance;
				vec2 Pos = mix(m_Pos, NewPos, a);
				for(int p = 0; p < MAX_CLIENTS; p++)
				{
					CCharacterCore *pCharCore = m_pWorld->m_apCharacters[p];
					if(!pCharCore || pCharCore == this)
						continue;
					if (!m_Infected && !pCharCore->m_Infected)
						continue;
					if ((m_Infected && pCharCore->m_Infected) && (m_HookProtected || pCharCore->m_HookProtected))
						continue;
					float D = distance(Pos, pCharCore->m_Pos);
					if(D < 28.0f && D >= 0.0f)
					{
						if(a > 0.0f)
							m_Pos = LastPos;
						else if(distance(NewPos, pCharCore->m_Pos) > D)
							m_Pos = NewPos;
						return;
					}
				}
				LastPos = Pos;
			}
		}
	}

	m_Pos = NewPos;
}

void CCharacterCore::Write(CNetObj_CharacterCore *pObjCore)
{
	pObjCore->m_X = round(m_Pos.x);
	pObjCore->m_Y = round(m_Pos.y);

	pObjCore->m_VelX = round(m_Vel.x*256.0f);
	pObjCore->m_VelY = round(m_Vel.y*256.0f);
	pObjCore->m_HookState = m_HookState;
	pObjCore->m_HookTick = m_HookTick;
	pObjCore->m_HookX = round(m_HookPos.x);
	pObjCore->m_HookY = round(m_HookPos.y);
	pObjCore->m_HookDx = round(m_HookDir.x*256.0f);
	pObjCore->m_HookDy = round(m_HookDir.y*256.0f);
	pObjCore->m_HookedPlayer = m_HookedPlayer;
	pObjCore->m_Jumped = m_Jumped;
	pObjCore->m_Direction = m_Direction;
	pObjCore->m_Angle = m_Angle;
}

void CCharacterCore::Read(const CNetObj_CharacterCore *pObjCore)
{
	m_Pos.x = pObjCore->m_X;
	m_Pos.y = pObjCore->m_Y;
	m_Vel.x = pObjCore->m_VelX/256.0f;
	m_Vel.y = pObjCore->m_VelY/256.0f;
	m_HookState = pObjCore->m_HookState;
	m_HookTick = pObjCore->m_HookTick;
	m_HookPos.x = pObjCore->m_HookX;
	m_HookPos.y = pObjCore->m_HookY;
	m_HookDir.x = pObjCore->m_HookDx/256.0f;
	m_HookDir.y = pObjCore->m_HookDy/256.0f;
	m_HookedPlayer = pObjCore->m_HookedPlayer;
	m_Jumped = pObjCore->m_Jumped;
	m_Direction = pObjCore->m_Direction;
	m_Angle = pObjCore->m_Angle;
}

void CCharacterCore::Quantize()
{
	CNetObj_CharacterCore Core;
	Write(&Core);
	Read(&Core);
}

bool CCharacterCore::IsRecursePassenger(CCharacterCore *pMaybePassenger) const
{
	if(m_Passenger)
	{
		if(m_Passenger == pMaybePassenger)
		{
			return true;
		}

		return m_Passenger->IsRecursePassenger(pMaybePassenger);
	}

	return false;
}

void CCharacterCore::SetPassenger(CCharacterCore *pPassenger)
{
	if(m_Passenger == pPassenger)
		return;

	if (m_Passenger)
	{
		m_Passenger->m_IsPassenger = false;
		m_Passenger->m_ProbablyStucked = true;
	}
	m_Passenger = pPassenger;
	if (pPassenger)
	{
		m_Passenger->m_IsPassenger = true;
	}
}

void CCharacterCore::EnableJump()
{
	m_Jumped &= ~2;
}

void CCharacterCore::UpdateTaxiPassengers()
{
	// InfClassR taxi mode, todo: cleanup & move out from core
	if(m_Passenger && ((m_Passenger->m_Input.m_Jump > 0) || (!m_Passenger->m_IsPassenger)))
	{
		SetPassenger(nullptr);
	}

	if(m_IsPassenger)
	{
		// Do nothing
	}
	else
	{
		int PassengerNumber = 0;
		CCharacterCore *pPassenger = m_Passenger;
		while(pPassenger)
		{
			++PassengerNumber;

			pPassenger->m_Vel = m_Vel;
			if(abs(pPassenger->m_Vel.y) <= 1.0f)
				pPassenger->m_Vel.y = 0.0f;

			pPassenger->m_Pos.x = m_Pos.x;
			pPassenger->m_Pos.y = m_Pos.y + PassengerYOffset * PassengerNumber;

			pPassenger = pPassenger->m_Passenger;
		}
	}
}
