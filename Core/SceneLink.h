//---------------------------------------------------------------------------
// SceneLink.h - links the 3D viewport (TViewport3D) with the node graph of the 2D viewport
// (TViewport2D). Header-only; include it after Viewport3D.h and Viewport2D.h.
//
// Every 3D object (Box / Plane) has a node in the 2D view and the other way round:
//   * a new 3D object  -> a new node appears in 2D          (name, position, rotation, size)
//   * a new 2D Box / Plane node -> a new object appears in 3D
//   * deleting on either side deletes the partner on the other side
//   * renaming on either side renames the partner
//   * moving / rotating / scaling an object in 3D updates the values of its node, so the
//     "Run" window shows the same scene (node inputs that are connected still win)
//   * selecting objects in 3D selects their nodes in 2D
//
// Coordinates: 3D is Z-up, the run window is Y-up:  run(X, Y, Z) = 3D(x, z, y),
// rotation = 3D rotation around Z (degrees), scale = full edge length of a box (plane: edge / 5).
// A box is a cube in the node graph, so a non-uniform 3D size shows up there as its X size.
//
// Call SceneLink::Sync(Viewport3D, Viewport2D) from a timer (Form1 does it every 100 ms).
//---------------------------------------------------------------------------
#ifndef SceneLinkH
#define SceneLinkH
//---------------------------------------------------------------------------
#include "Viewport3D.h"
#include "Viewport2D.h"
#include <vector>
#include <set>
#include <map>
#include <math.h>
//---------------------------------------------------------------------------
namespace SceneLink
{
	inline void ToVals(const TSceneObj &O, float V[5])
	{
		bool Plane = (O.Kind == L"Plane");
		V[0] = O.Pos.x;
		V[1] = O.Pos.z;
		V[2] = O.Pos.y;
		V[3] = O.Rot.z * 57.2957795f;
		V[4] = Plane ? O.Half.x / 2.5f : O.Half.x * 2.0f;
	}

	inline TSceneObj FromVals(int Kind, const String &Name, const float V[5])
	{
		TSceneObj O;
		O.Name = Name;
		O.Pos  = V3(V[0], V[2], V[1]);
		O.Rot  = V3(0, 0, V[3] * 0.0174532925f);
		float Sc = fabsf(V[4]);
		if (Sc < 0.05f) Sc = 0.05f;
		if (Kind == nkPlane)
		{
			O.Kind = L"Plane";
			float h = 2.5f * Sc;
			O.Half = V3(h, h, 0.05f);
			O.R = 100; O.G = 160; O.B = 210;
		}
		else
		{
			O.Kind = L"Box";
			float h = 0.5f * Sc;
			O.Half = V3(h, h, h);
			O.R = 210; O.G = 170; O.B = 80;
		}
		O.Uid = 0;
		return O;
	}

	// returns true when something was created
	inline bool Sync(TViewport3D *V3d, TViewport2D *V2d)
	{
		static std::set<int>         Known;		// uids that were linked at the last sync
		static std::map<int, String> LastName;	// name both sides had at the last sync
		static std::vector<int>      PrevSel;	// last 3D selection (copied to the 2D view when it changes)
		if (!V3d || !V2d)
			return false;
		bool Created = false;

		std::vector<TViewport2D::TLinkInfo> Nodes;
		V2d->GetLinkNodes(Nodes);
		std::set<int> Alive;
		for (int i = 0; i < V3d->ObjectCount(); i++)
			Alive.insert(V3d->Obj(i).Uid);
		std::set<int> Paired;					// uids that have a node

		// ---- 1) nodes ----
		for (size_t i = 0; i < Nodes.size(); i++)
		{
			TViewport2D::TLinkInfo &N = Nodes[i];
			if (N.Obj != 0 && Paired.count(N.Obj))		// two nodes claim one object: the second one is a new object
			{
				V2d->SetNodeObj(N.Id, 0);
				N.Obj = 0;
			}
			if (N.Obj != 0 && !Alive.count(N.Obj))
			{
				if (Known.count(N.Obj))					// the object was deleted in 3D
				{
					LastName.erase(N.Obj);
					V2d->RemoveNode(N.Id);
					continue;
				}
				N.Obj = 0;
			}
			if (N.Obj == 0)								// new node: create its 3D object
			{
				N.Obj = V3d->AddSceneObject(FromVals(N.Kind, N.Name, N.V));
				V2d->SetNodeObj(N.Id, N.Obj);
				Alive.insert(N.Obj);
				LastName[N.Obj] = N.Name;
			}
			Paired.insert(N.Obj);
		}

		// ---- 2) objects without a node ----
		std::vector<int> ToDelete;
		for (int i = 0; i < V3d->ObjectCount(); i++)
		{
			const TSceneObj &O = V3d->Obj(i);
			if (Paired.count(O.Uid))
				continue;
			if (Known.count(O.Uid))						// its node was deleted in 2D
			{
				ToDelete.push_back(O.Uid);
				continue;
			}
			float V[5];
			ToVals(O, V);
			V2d->AddLinkedNode((O.Kind == L"Plane") ? nkPlane : nkBox, O.Name, O.Uid, V);
			LastName[O.Uid] = O.Name;
			Paired.insert(O.Uid);
			Created = true;
		}
		std::set<int> NewKnown = Paired;
		for (size_t i = 0; i < ToDelete.size(); i++)
		{
			if (V3d->RemoveObjectByUid(ToDelete[i]))
				LastName.erase(ToDelete[i]);
			else
				NewKnown.insert(ToDelete[i]);			// could not delete right now: try again next time
		}

		// ---- 3) names and values of the pairs ----
		V2d->GetLinkNodes(Nodes);
		for (size_t i = 0; i < Nodes.size(); i++)
		{
			const TViewport2D::TLinkInfo &N = Nodes[i];
			if (N.Obj == 0)
				continue;
			int Idx = V3d->IndexOfUid(N.Obj);
			if (Idx < 0)
				continue;
			TSceneObj *O = V3d->MutableObj(Idx);
			String &Last = LastName[N.Obj];
			if (N.Name != Last)							// renamed in 2D
			{
				O->Name = N.Name;
				Last = N.Name;
			}
			else if (O->Name != Last)					// renamed in 3D
			{
				V2d->SetNodeName(N.Id, O->Name);
				Last = O->Name;
			}
			float V[5];
			ToVals(*O, V);
			V2d->SetNodeVals(N.Id, V);
		}
		Known = NewKnown;

		// ---- 4) selection: 3D -> 2D ----
		std::vector<int> Sel;
		V3d->GetSelectedUids(Sel);
		if (Created || Sel != PrevSel)
		{
			V2d->SelectNodesByObj(Sel);
			PrevSel = Sel;
		}
		return Created;
	}
}
//---------------------------------------------------------------------------
#endif
