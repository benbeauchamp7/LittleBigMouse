#include "MouseEngine.h"
#include "MouseHookerWindowsHook.h"
#include <iostream>
#include <windows.h>

#include "Geometry.h"
#include "tinyxml2.h"
#include "ZoneLink.h"
#include "RemoteServer.h"
#include "Zone.h"

void MouseEngine::OnMouseMoveExtFirst(HookMouseEventArg& e)
{
	_oldPoint = e.Point;

	_oldZone = Layout.Containing(_oldPoint);

	if (!_oldZone) return;

	switch (Layout.Algorithm)
	{
	case Strait:
		OnMouseMoveFunc = &MouseEngine::OnMouseMoveStraight;
		break;
	case CornerCrossing:
		OnMouseMoveFunc = &MouseEngine::OnMouseMoveCross;
		break;
	}
}

void MouseEngine::SaveClip(const geo::Rect<long>& r)
{
	_oldClipRect = MouseHookerWindowsHook::GetClip();
}

void MouseEngine::ResetClip()
{
	if (!_oldClipRect.IsEmpty())
	{
		MouseHookerWindowsHook::SetClip(_oldClipRect);
		_oldClipRect = geo::Rect<long>::Empty();
	}
}

// no zone found at mouse position
void MouseEngine::NoZoneMatches(HookMouseEventArg& e)
{
	// Store current clip zone to be restored at next move
	// Clip to current zone to get cursor back
	SaveClip(_oldZone->PixelsBounds());

	e.Handled = true;// when set to true, cursor stick to frame

#ifdef _DEBUG_
	const auto p = _oldZone->ToPhysical(e.Point);
	std::cout << "NoZoneMatches : " << _oldZone->Name << e.Point << p << "\n";
#endif

}


Zone* MouseEngine::FindTargetZone(const Zone* current, const geo::Segment<double>& trip, geo::Point<double>& pOutInMm, double minDist) const
{
	Zone* zoneOut = nullptr;
	pOutInMm = trip.B();
	const auto tripLine = trip.Line();

	// Check all zones for borders intersections with mouse direction.
	for (const auto zone : Layout.Zones)
	{
		// exclude zone we are currently in.
		if (current == zone) continue;

		// if new point is within zone lets use it (it may append when allowing zones overlap)
		if (zone->Contains(trip.B()))
		{
			zoneOut = zone;
			minDist = 0;
#ifdef _DEBUG_
			std::cout << "#";
#endif
		}
		else
		{
			//check intersection between the line and the four borders of the target zone
			for (auto p : zone->PhysicalInside().Intersect(tripLine))
			{
#ifdef _DEBUG_
				std::cout << zone->Name << p;
#endif
				if (p.X()<trip.A().X()) 
				{
					if (trip.B().X() > trip.A().X()) 
					{

#ifdef _DEBUG_
						std::cout << "<\n";
#endif
						continue;
					}
				}
				else
				{
					if (trip.B().X() < trip.A().X()) 
					{
#ifdef _DEBUG_
						std::cout << ">\n";
#endif
						continue;
					}
				}

				if (p.Y()<trip.A().Y()) 
				{
					if (trip.B().Y() > trip.A().Y())
					{
#ifdef _DEBUG_
						std::cout << "/\\\n";
#endif
						continue;
					}
				}
				else
				{
					if (trip.B().Y() < trip.A().Y()) 
					{
#ifdef _DEBUG_
						std::cout << "\\/\n";
#endif
						continue;
					}
				}

				// calculate distance (squared) to retain the intersection with minimal travel distance
				const auto dist = pow(p.X() - trip.B().X(), 2) + pow(p.Y() - trip.B().Y(), 2);

				if (dist > minDist) {
#ifdef _DEBUG_
					std::cout << "+\n";
#endif
					continue;
				}

#ifdef _DEBUG_
				std::cout << "*\n";
#endif
				minDist = dist;
				zoneOut = zone;
				pOutInMm = p;
			}
		}
	}
	return zoneOut;
}

void MouseEngine::OnMouseMoveCross(HookMouseEventArg& e)
{
	ResetClip();


	if (_oldZone->PixelsBounds().Contains(e.Point))
	{
#ifdef _DEBUG_
		std::cout << "no change : " << pIn << "\n";
#endif

		_oldPoint = e.Point;
		e.Handled = false;
		return;
	}

	const auto pInMmOld = _oldZone->ToPhysical(_oldPoint);
	const auto pInMm = _oldZone->ToPhysical(e.Point);

	//Get line from previous point to current point
	const auto trip = geo::Segment<double>(pInMmOld, pInMm);
	const auto minDist = DBL_MAX;

	geo::Point<double> pOutInMm;

	if(const auto zoneOut = FindTargetZone(_oldZone, trip, pOutInMm, minDist))
	{
		MoveInMm(e, pOutInMm, zoneOut);
		return;
	}

	if(Layout.LoopX)
	{
		const Zone* zoneOut = nullptr;
		// we are moving left
		if(trip.B().X() < trip.A().X())
			zoneOut = FindTargetZone(nullptr, trip + geo::Point<double>(Layout.Width(),0), pOutInMm, minDist);

		// we are moving right
		else if(trip.B().X() > trip.A().X())
			zoneOut = FindTargetZone(nullptr, trip - geo::Point<double>(Layout.Width(),0), pOutInMm, minDist);

		if(zoneOut)
		{
			MoveInMm(e, pOutInMm, zoneOut);
			return;
		}
	}

	if(Layout.LoopY)
	{
		const Zone* zoneOut = nullptr;

		// we are moving top
		if(trip.B().Y() < trip.A().Y())
			zoneOut = FindTargetZone(nullptr, trip + geo::Point<double>(0,Layout.Height()), pOutInMm, minDist);

		// we are moving bottom
		else if(trip.B().Y() > trip.A().Y())
			zoneOut = FindTargetZone(nullptr, trip - geo::Point<double>(0,Layout.Height()), pOutInMm, minDist);

		if(zoneOut) 
		{
			MoveInMm(e, pOutInMm, zoneOut);
			return;
		}
	}
#ifdef _DEBUG_
	std::cout << " - " << pInMmOld << pInMm << " - ";
#endif
	NoZoneMatches(e);
}

void MouseEngine::OnMouseMoveStraight(HookMouseEventArg& e)
{
	const auto pIn = e.Point;
	ResetClip();

	const ZoneLink* zoneOut;
	geo::Point<long> pOut;
	const auto bounds = _oldZone->PixelsBounds();
	// leaving zone by right
	if (pIn.X() >= bounds.Right())
	{
		zoneOut = _oldZone->RightZones->AtPixel(pIn.Y());
		if (zoneOut->Target)
		{
			pOut = { zoneOut->Target->PixelsBounds().Left(),zoneOut->ToTargetPixel(pIn.Y()) };
		}
		else
		{
			NoZoneMatches(e);
			return;
		}
	}
	// leaving zone by left
	else if (pIn.X() < bounds.Left())
	{
		zoneOut = _oldZone->LeftZones->AtPixel(pIn.Y());
		if (zoneOut->Target)
		{
			pOut = { zoneOut->Target->PixelsBounds().Right() - 1,zoneOut->ToTargetPixel(pIn.Y()) };
		}
		else
		{
			NoZoneMatches(e);
			return;
		}
	}
	// leaving zone by bottom
	else if (pIn.Y() >= bounds.Bottom())
	{
		zoneOut = _oldZone->BottomZones->AtPixel(pIn.Y());
		if (zoneOut->Target)
		{
			pOut = { zoneOut->ToTargetPixel(pIn.X()), zoneOut->Target->PixelsBounds().Top() };
		}
		else
		{
			NoZoneMatches(e);
			return;
		}
	}
	// leaving zone by top
	else if (pIn.Y() < _oldZone->PixelsBounds().Top())
	{
		zoneOut = _oldZone->TopZones->AtPixel(pIn.X());
		if (zoneOut->Target)
		{
			pOut = { zoneOut->ToTargetPixel(pIn.X()),zoneOut->Target->PixelsBounds().Bottom() - 1 };
		}
		else
		{
			NoZoneMatches(e);
			return;
		}
	}
	else
	{
		_oldPoint = pIn;
		e.Handled = false;
		return;
	}

	Move(e, pOut, zoneOut->Target);
}

void MouseEngine::MoveInMm(HookMouseEventArg& e, const geo::Point<double>& pOutInMm, const Zone* zoneOut)
{
	const auto pOut = zoneOut->ToPixels(pOutInMm);

	Move(e, pOut, zoneOut);
}

void MouseEngine::Move(HookMouseEventArg& e, const geo::Point<long>& pOut, const Zone* zoneOut)
{
	const auto travel = _oldZone->TravelPixels(Layout.MainZones, zoneOut);

	_oldZone = zoneOut->Main;
	_oldPoint = pOut;

	const auto r = zoneOut->PixelsBounds();

	_oldClipRect = MouseHookerWindowsHook::GetClip();
	auto pos = e.Point;

	for (const auto& rect : travel)
	{

		if (rect.Contains(pos)) continue;

		MouseHookerWindowsHook::SetClip(rect);

		pos = MouseHookerWindowsHook::GetMouseLocation();

		if (rect.Contains(pOut)) break;
	}

	MouseHookerWindowsHook::SetClip(r);
	MouseHookerWindowsHook::SetMouseLocation(pOut);

	_oldClipRect = MouseHookerWindowsHook::GetClip();
	MouseHookerWindowsHook::SetMouseLocation(pOut);

#ifdef _DEBUG_
	std::cout << "moved : " << zoneOut->Name << " at " << pOut << "\n";
#endif

	e.Handled = true;
}

void MouseEngine::RunThread()
{
	MouseHookerWindowsHook::Start(*this);
}

void MouseEngine::Send(const std::string& message) const
{
	if (_remote) _remote->Send(message);
}

void MouseEngine::OnMouseMove(HookMouseEventArg& e)
{
	_lock.lock();
	(this->*OnMouseMoveFunc)(e);
	_lock.unlock();

	//if(e.Timing())
	//{
	//	std::cout << e.GetDuration() << "\n";
	//}
}
void MouseEngine::DoStop()
{
	MouseHookerWindowsHook::Stopping = true;
}

void MouseEngine::OnStopped()
{
	OnMouseMoveFunc = &MouseEngine::OnMouseMoveExtFirst;
}
