//	CParticleDamage.cpp
//
//	CParticleDamage class

#include "PreComp.h"

static CObjectClass<CParticleDamage>g_Class(OBJID_CPARTICLEDAMAGE, NULL);

CParticleDamage::CParticleDamage (void) : CSpaceObject(&g_Class),
		m_pEnhancements(NULL),
		m_pPainter(NULL)

//	CParticleDamage constructor

	{
	}

CParticleDamage::~CParticleDamage (void)

//	CParticleDamage destructor

	{
	if (m_pPainter)
		m_pPainter->Delete();

	if (m_pEnhancements)
		m_pEnhancements->Delete();
	}

ALERROR CParticleDamage::Create (CSystem *pSystem,
								 CWeaponFireDesc *pDesc,
								 CItemEnhancementStack *pEnhancements,
								 DestructionTypes iCause,
								 const CDamageSource &Source,
								 const CVector &vPos,
								 const CVector &vVel,
								 int iDirection,
								 CSpaceObject *pTarget,
								 CParticleDamage **retpObj)

//	Create
//
//	Create the object

	{
	ALERROR error;

	//	We better have a particle description.

	const CParticleSystemDesc *pSystemDesc = pDesc->GetParticleSystemDesc();
	ASSERT(pSystemDesc);
	if (pSystemDesc == NULL)
		return ERR_FAIL;

	//	Make sure we have a valid CWeaponFireDesc (otherwise we won't be
	//	able to save the object).

	ASSERT(!pDesc->m_sUNID.IsBlank());

	//	Create the area

	CParticleDamage *pParticles = new CParticleDamage;
	if (pParticles == NULL)
		return ERR_MEMORY;

	pParticles->Place(vPos, CVector());

	//	Get notifications when other objects are destroyed
	pParticles->SetObjectDestructionHook();

	//	Set non-linear move, meaning that we are responsible for
	//	setting the position and velocity in OnMove
	pParticles->SetNonLinearMove();

	pParticles->m_pDesc = pDesc;
	pParticles->m_pTarget = pTarget;
	pParticles->m_pEnhancements = (pEnhancements ? pEnhancements->AddRef() : NULL);
	pParticles->m_iCause = iCause;
	pParticles->m_iEmitTime = Max(1, pSystemDesc->GetEmitLifetime().Roll());
	pParticles->m_iLifeLeft = pDesc->GetMaxLifetime() + pParticles->m_iEmitTime;
	pParticles->m_iTick = 0;

	//	Keep track of where we emitted particles relative to the source. We
	//	need this so we can continue to emit from this location later.

	pParticles->m_Source = Source;
	if (!Source.IsEmpty())
		{
		//	Decompose the source position/velocity so that we can continue to
		//	emit later (after the source has changed).
		//
		//	We start by computing the emission offset relative to the source
		//	object when it points at 0 degrees.

		int iSourceRotation = Source.GetObj()->GetRotation();
		CVector vPosOffset = (vPos - Source.GetObj()->GetPos()).Rotate(-iSourceRotation);

		//	Remember these values so we can add them to the new source 
		//	position/velocity.

		pParticles->m_iEmitDirection = iDirection - iSourceRotation;
		pParticles->m_vEmitSourcePos = vPosOffset;
		pParticles->m_vEmitSourceVel = CVector();
		}
	else
		{
		pParticles->m_iEmitDirection = iDirection;
		pParticles->m_vEmitSourcePos = CVector();
		pParticles->m_vEmitSourceVel = CVector();
		}

	//	Damage

	pParticles->m_iDamage = pDesc->m_Damage.RollDamage();

	//	Friendly fire

	if (!pDesc->CanHitFriends())
		pParticles->SetNoFriendlyFire();

	//	Painter

	pParticles->m_pPainter = pDesc->CreateEffectPainter();

	//	Remember the sovereign of the source (in case the source is destroyed)

	if (Source.GetObj())
		pParticles->m_pSovereign = Source.GetObj()->GetSovereign();
	else
		pParticles->m_pSovereign = NULL;

	//	Compute the maximum number of particles that we might have

	int iMaxCount = pParticles->m_iEmitTime * pSystemDesc->GetEmitRate().GetMaxValue();
	pParticles->m_Particles.Init(iMaxCount);

	//	Create the initial particles.
	//
	//	NOTE: We use the source velocity (instead of vVel) because Emit expects
	//	to add the particle velocity.

	int iInitCount;
	pParticles->m_Particles.Emit(*pSystemDesc, 
			vPos - pParticles->GetOrigin(), 
			(!Source.IsEmpty() ? Source.GetObj()->GetVel() : CVector()), 
			iDirection, 
			0, 
			&iInitCount);

	//	Figure out the number of particles that will cause full damage

	if (pParticles->m_iEmitTime > 1)
		pParticles->m_iParticleCount = pParticles->m_iEmitTime * pSystemDesc->GetEmitRate().GetAveValue();
	else
		pParticles->m_iParticleCount = iInitCount;

	pParticles->m_iParticleCount = Max(1, pParticles->m_iParticleCount);

	//	Add to system

	if (error = pParticles->AddToSystem(pSystem))
		{
		delete pParticles;
		return error;
		}

	//	Done

	if (retpObj)
		*retpObj = pParticles;

	return NOERROR;
	}

CString CParticleDamage::GetName (DWORD *retdwFlags)

//	GetName
//
//	Returns the name of the object

	{
	//	This name is used only if the source has been destroyed

	if (retdwFlags)
		*retdwFlags = 0;
	return CONSTLIT("enemy weapon");
	}

void CParticleDamage::OnDestroyed (SDestroyCtx &Ctx)

//	OnDestroyed
//
//	Shot destroyed

	{
	m_pDesc->FireOnDestroyShot(this);
	}

void CParticleDamage::OnMove (const CVector &vOldPos, Metric rSeconds)

//	OnMove
//
//	Handle moving

	{
	//	Update the single particle painter

	if (m_pPainter)
		{
		SEffectMoveCtx Ctx;
		Ctx.pObj = this;

		m_pPainter->OnMove(Ctx);
		}

	//	Update particle motion

	bool bAlive;
	CVector vNewPos;
	m_Particles.UpdateMotionLinear(&bAlive, &vNewPos);

	//	If no particles are left alive, then we destroy the object

	if (!bAlive)
		{
		Destroy(removedFromSystem, CDamageSource());
		return;
		}

	//	Set the position of the object based on the average particle position

	SetPos(vNewPos);

	//	Set the bounds (note, we make the bounds twice as large to deal
	//	with the fact that we're moving).

	RECT rcBounds = m_Particles.GetBounds();
	SetBounds(g_KlicksPerPixel * Max(RectWidth(rcBounds), RectHeight(rcBounds)));
	}

void CParticleDamage::ObjectDestroyedHook (const SDestroyCtx &Ctx)

//	ObjectDestroyedHook
//
//	Called when another object is destroyed

	{
	m_Source.OnObjDestroyed(Ctx.pObj);

	if (Ctx.pObj == m_pTarget)
		m_pTarget = NULL;
	}

void CParticleDamage::OnPaint (CG32bitImage &Dest, int x, int y, SViewportPaintCtx &Ctx)

//	OnPaint
//
//	Paint

	{
	const CParticleSystemDesc *pSystemDesc = m_pDesc->GetParticleSystemDesc();
	ASSERT(pSystemDesc);
	if (pSystemDesc == NULL)
		return;

	CViewportPaintCtxSmartSave Save(Ctx);
	Ctx.iTick = m_iTick;
	Ctx.iVariant = 0;
	Ctx.iRotation = 0;
	Ctx.iDestiny = GetDestiny();
	Ctx.iMaxLength = (int)((g_SecondsPerUpdate * Max(1, m_iTick) * m_pDesc->GetRatedSpeed()) / g_KlicksPerPixel);

	//	Painting is relative to the origin

	int xOrigin, yOrigin;
	Ctx.XForm.Transform(GetOrigin(), &xOrigin, &yOrigin);

	//	If we can get a paint descriptor, use that because it is faster

	m_Particles.Paint(*pSystemDesc, Dest, xOrigin, yOrigin, m_pPainter, Ctx);
	}

void CParticleDamage::OnReadFromStream (SLoadCtx &Ctx)

//	OnReadFromStream
//
//	Restore from stream

	{
	DWORD dwLoad;

#ifdef DEBUG_LOAD
	::OutputDebugString("CParticleDamage::OnReadFromStream\n");
#endif

	//	Load descriptor

	CString sDescUNID;
	sDescUNID.ReadFromStream(Ctx.pStream);
	m_pDesc = g_pUniverse->FindWeaponFireDesc(sDescUNID);

	//	Old style bonus

	if (Ctx.dwVersion < 92)
		{
		int iBonus;
		Ctx.pStream->Read((char *)&iBonus, sizeof(DWORD));
		if (iBonus != 0)
			{
			m_pEnhancements = new CItemEnhancementStack;
			m_pEnhancements->InsertHPBonus(iBonus);
			}
		}

	//	Load other stuff

	if (Ctx.dwVersion >= 18)
		{
		Ctx.pStream->Read((char *)&dwLoad, sizeof(DWORD));
		m_iCause = (DestructionTypes)dwLoad;
		}
	else
		m_iCause = killedByDamage;

	Ctx.pStream->Read((char *)&m_iLifeLeft, sizeof(m_iLifeLeft));
	m_Source.ReadFromStream(Ctx);
	CSystem::ReadSovereignRefFromStream(Ctx, &m_pSovereign);
	Ctx.pStream->Read((char *)&m_iTick, sizeof(m_iTick));
	Ctx.pStream->Read((char *)&m_iDamage, sizeof(m_iDamage));
	if (Ctx.dwVersion >= 3 && Ctx.dwVersion < 67)
		{
		CVector vDummy;
		Ctx.pStream->Read((char *)&vDummy, sizeof(CVector));
		}

	//	The newer version uses a different particle array

	if (Ctx.dwVersion >= 21)
		{
		Ctx.pStream->Read((char *)&m_vEmitSourcePos, sizeof(CVector));
		Ctx.pStream->Read((char *)&m_vEmitSourceVel, sizeof(CVector));
		Ctx.pStream->Read((char *)&m_iEmitDirection, sizeof(DWORD));
		Ctx.pStream->Read((char *)&m_iEmitTime, sizeof(DWORD));
		Ctx.pStream->Read((char *)&m_iParticleCount, sizeof(DWORD));

		//	Load painter

		m_pPainter = CEffectCreator::CreatePainterFromStreamAndCreator(Ctx, m_pDesc->GetEffect());

		m_Particles.ReadFromStream(Ctx);
		}

	//	Read the previous version, but no need to convert

	else
		{
		DWORD dwCount;
		Ctx.pStream->Read((char *)&dwCount, sizeof(DWORD));
		if (dwCount > 0)
			{
			char *pDummy = new char [5 * sizeof(DWORD) * dwCount];
			Ctx.pStream->Read(pDummy, 5 * sizeof(DWORD) * dwCount);
			delete pDummy;
			}

		m_iEmitTime = 0;
		m_iEmitDirection = -1;
		}

	//	Read the target

	if (Ctx.dwVersion >= 67)
		CSystem::ReadObjRefFromStream(Ctx, &m_pTarget);
	else
		m_pTarget = NULL;

	//	Enhancements

	if (Ctx.dwVersion >= 92)
		CItemEnhancementStack::ReadFromStream(Ctx, &m_pEnhancements);
	}

void CParticleDamage::OnUpdate (SUpdateCtx &Ctx, Metric rSecondsPerTick)

//	OnUpdate
//
//	Update

	{
	DEBUG_TRY

	const CParticleSystemDesc *pSystemDesc = m_pDesc->GetParticleSystemDesc();
	ASSERT(pSystemDesc);
	if (pSystemDesc == NULL)
		return;

	m_iTick++;

	//	Update the single particle painter

	if (m_pPainter)
		m_pPainter->OnUpdate();

	//	Set up context block for particle array update

	SEffectUpdateCtx EffectCtx;
	EffectCtx.pSystem = GetSystem();
	EffectCtx.pObj = this;
	EffectCtx.iTick = m_iTick;

	EffectCtx.pDamageDesc = m_pDesc;
	EffectCtx.iTotalParticleCount = m_iParticleCount;
	EffectCtx.pEnhancements = m_pEnhancements;
	EffectCtx.iCause = m_iCause;
	EffectCtx.bAutomatedWeapon = IsAutomatedWeapon();
	EffectCtx.Attacker = m_Source;
	EffectCtx.pTarget = m_pTarget;

	//	Update (includes doing damage)

	m_Particles.Update(*pSystemDesc, EffectCtx);

	//	Expired?

	if (--m_iLifeLeft <= 0)
		{
		Destroy(removedFromSystem, CDamageSource());
		return;
		}

	//	Emit new particles

	if (m_iTick < m_iEmitTime && !m_Source.IsEmpty())
		{
		//	Rotate the offsets appropriately

		int iRotation = m_Source.GetObj()->GetRotation();
		CVector vPos = m_vEmitSourcePos.Rotate(iRotation);
		CVector vVel = m_vEmitSourceVel.Rotate(iRotation);

		//	Emit

		m_Particles.Emit(*pSystemDesc, 
				m_Source.GetObj()->GetPos() + vPos - GetOrigin(), 
				m_Source.GetObj()->GetVel() + vVel,
				AngleMod(iRotation + m_iEmitDirection),
				m_iTick);
		}

	DEBUG_CATCH
	}

void CParticleDamage::OnWriteToStream (IWriteStream *pStream)

//	OnWriteToStream
//
//	Write out to stream
//
//	CString			CWeaponFireDesc UNID
//	DWORD			m_iLifeLeft
//	DWORD			m_Source (CSpaceObject ref)
//	DWORD			m_pSovereign (CSovereign ref)
//	DWORD			m_iTick
//	DWORD			m_iDamage
//
//	CVector			m_vEmitSourcePos
//	CVector			m_vEmitSourceVel
//	DWORD			m_iEmitDirection
//	DWORD			m_iEmitTime
//	DWORD			m_iParticleCount
//	IEffectPainter
//	CParticleArray
//
//	CSpaceObject	m_pTarget
//
//	CItemEnhancementStack	m_pEnhancements

	{
	DWORD dwSave;
	m_pDesc->m_sUNID.WriteToStream(pStream);
	dwSave = m_iCause;
	pStream->Write((char *)&dwSave, sizeof(DWORD));
	pStream->Write((char *)&m_iLifeLeft, sizeof(m_iLifeLeft));
	m_Source.WriteToStream(GetSystem(), pStream);
	GetSystem()->WriteSovereignRefToStream(m_pSovereign, pStream);
	pStream->Write((char *)&m_iTick, sizeof(m_iTick));
	pStream->Write((char *)&m_iDamage, sizeof(m_iDamage));
	pStream->Write((char *)&m_vEmitSourcePos, sizeof(CVector));
	pStream->Write((char *)&m_vEmitSourceVel, sizeof(CVector));
	pStream->Write((char *)&m_iEmitDirection, sizeof(DWORD));
	pStream->Write((char *)&m_iEmitTime, sizeof(DWORD));
	pStream->Write((char *)&m_iParticleCount, sizeof(DWORD));

	CEffectCreator::WritePainterToStream(pStream, m_pPainter);

	m_Particles.WriteToStream(pStream);

	WriteObjRefToStream(m_pTarget, pStream);

	//	Enhancements

	CItemEnhancementStack::WriteToStream(m_pEnhancements, pStream);
	}

bool CParticleDamage::PointInObject (const CVector &vObjPos, const CVector &vPointPos)

//	PointInObject
//
//	Returns TRUE if the given point is in the object

	{
	return false;
	}
