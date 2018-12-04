//	CItemListManipulator.cpp
//
//	CItemListManipulator object

#include "PreComp.h"


#define LOOKUP_TAG							CONSTLIT("Lookup")
#define ITEM_TAG							CONSTLIT("Item")
#define NULL_TAG							CONSTLIT("Null")

#define TABLE_ATTRIB						CONSTLIT("table")
#define ITEM_ATTRIB							CONSTLIT("item")
#define CATEGORIES_ATTRIB					CONSTLIT("categories")
#define LEVEL_ATTRIB						CONSTLIT("level")
#define DAMAGED_ATTRIB						CONSTLIT("damaged")

CItemListManipulator::CItemListManipulator (CItemList &ItemList) : m_ItemList(ItemList),
		m_iCursor(-1),
		m_bUseFilter(false)

//	CItemListManipulator constructor

	{
	GenerateViewMap();
	CItem::InitCriteriaAll(&m_Filter);
	}

CItemListManipulator::~CItemListManipulator (void)

//	CItemListManipulator destructor

	{
	}

bool CItemListManipulator::AddDamagedComponents (const CItem &Item, int iDamageChance)

//	AddDamagedComponents
//
//	When a ship/station is destroyed, installed items get added to the wreck in
//	various states of damage. This function will add the appropriate remains 
//	to the wreck, based on the damage chance and the composition of the item.
//
//	Returns TRUE if any item got added.

	{
	//	We handle armor and devices differently.

	if (Item.IsArmor())
		{
		//	If not (very) damaged and not virtual, then this item drops intact.

		if (iDamageChance <= 5 && !Item.IsVirtual())
			{
			CItem ItemToDrop(Item);
			ItemToDrop.ClearDamaged();
			ItemToDrop.SetInstalled(-1);

			AddItem(ItemToDrop);
			return true;
			}

		//	If we have components, then some components drop, in proportion to 
		//	damage chance. NOTE: This works even if the item is virtual.

		else if (Item.HasComponents() && (Item.IsVirtual() || mathRandom(1, 100) <= 50))
			{
			//	Max 80% chance of any one component surviving; lower if the armor
			//	is damaged.

			return AddItems(Item.GetComponents(), Min(80, 100 - iDamageChance));
			}

		//	If the item is virtual, then nothing drops

		else if (Item.IsVirtual())
			return false;

		//	If the armor is too damaged, then nothing drops

		else if (iDamageChance >= 75)
			return false;

		//	Otherwise, we drop damaged armor

		else
			{
			CItem ItemToDrop(Item);
			ItemToDrop.SetDamaged();
			ItemToDrop.SetInstalled(-1);

			AddItem(ItemToDrop);
			return true;
			}
		}

	//	Handle devices

	else if (Item.IsDevice())
		{
		//	Roll to see if device is damaged

		bool bDropDamaged = (mathRandom(1, 100) <= iDamageChance);

		//	If we have components, we usually drop the components.

		if (Item.HasComponents() && (Item.IsVirtual() || mathRandom(1, 100) <= 50))
			{
			//	If we're not virtual and not damaged, then we drop intact.

			if (!Item.IsVirtual() && !Item.IsDamaged() && !bDropDamaged)
				{
				CItem ItemToDrop(Item);
				ItemToDrop.SetInstalled(-1);

				AddItem(ItemToDrop);
				return true;
				}

			//	Otherwise, we drop components

			else
				{
				//	Compute the chance that a component will drop.

				int iChance = (bDropDamaged ? 50 : 80);
				if (Item.IsDamaged())
					iChance /= 2;

				//	Add components

				return AddItems(Item.GetComponents(), iChance);
				}
			}

		//	Virtual devices never drop

		else if (Item.IsVirtual())
			return false;

		//	If we're not damaged more, then we drop intact

		else if (!bDropDamaged)
			{
			CItem ItemToDrop(Item);
			ItemToDrop.SetInstalled(-1);

			AddItem(ItemToDrop);
			return true;
			}

		//	If we're already damaged, then we never drop

		else if (Item.IsDamaged())
			return false;

		//	We drop a damaged device

		else
			{
			CItem ItemToDrop(Item);
			ItemToDrop.SetInstalled(-1);

			if (Item.GetMods().IsEnhancement())
				{
				CItemEnhancement Mods(Item.GetMods());
				Mods.Combine(Item, etLoseEnhancement);
				ItemToDrop.AddEnhancement(Mods);
				}
			else if (ItemToDrop.IsEnhanced())
				ItemToDrop.ClearEnhanced();
			else
				ItemToDrop.SetDamaged();

			AddItem(ItemToDrop);
			return true;
			}
		}

	//	Nothing

	else
		return false;
	}

void CItemListManipulator::AddItem (const CItem &Item)

//	AddItem
//
//	Adds an item to the list and puts the cursor on the newly added item. Note
//	that this will fold similar items together, so the item at the cursor is not
//	guaranteed to be the same count.

	{
	int iIndex = FindItem(Item);
	if (iIndex != -1)
		{
		CItem &ThisItem = m_ItemList.GetItem(m_ViewMap[iIndex]);
		ThisItem.SetCount(Item.GetCount() + ThisItem.GetCount());
		m_iCursor = iIndex;
		}
	else
		{
		m_iCursor = m_ViewMap.GetCount();
		m_ViewMap.Insert(m_ItemList.GetCount());
		m_ItemList.AddItem(Item);
		}
	}

DWORD CItemListManipulator::AddItemEnhancementAtCursor (const CItemEnhancement &Mods, int iCount)

//	AddItemEnhancementAtCursor
//
//	Adds the given item enhancement and returns the ID

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	NewItem.AddEnhancement(Mods);
	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));

	MoveItemTo(NewItem, OldItem);

	return GetItemAtCursor().GetMods().GetID();
	}

bool CItemListManipulator::AddItems (const CItemList &ItemList, int iChance)

//	AddItems
//
//	Adds all the items in the list. Returns TRUE if we added any items.

	{
	int i;
	bool bAdded = false;

	if (iChance == 100)
		{
		for (i = 0; i < ItemList.GetCount(); i++)
			{
			AddItem(ItemList.GetItem(i));
			bAdded = true;
			}
		}
	else
		{
		Metric rChance = (Metric)iChance / 100.0;

		for (i = 0; i < ItemList.GetCount(); i++)
			{
			CItem NewItem = ItemList.GetItem(i);

			//	Skip virtual

			if (NewItem.IsVirtual())
				continue;

			//	Add with probability

			int iCount = mathRoundStochastic(rChance * NewItem.GetCount());
			if (iCount > 0)
				{
				NewItem.SetCount(iCount);
				AddItem(NewItem);
				bAdded = true;
				}
			}
		}

	return bAdded;
	}

void CItemListManipulator::ClearDisruptedAtCursor (int iCount)

//	ClearDisruptedAtCursor
//
//	Clears the disrupted time

	{
	ASSERT(m_iCursor != -1);

	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	//	Generate a new item

	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));
	NewItem.ClearDisrupted();

	MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::DeleteAtCursor (int iCount)

//	DeleteAtCursor
//
//	Deletes a certain number of copies at the cursor. If
//	All the copies of the item are deleted, the entire item
//	is deleted.

	{
	ASSERT(m_iCursor != -1);

	CItem &ThisItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	if (ThisItem.GetCount() > iCount)
		ThisItem.SetCount(ThisItem.GetCount() - iCount);
	else
		{
		m_ItemList.DeleteItem(m_ViewMap[m_iCursor]);
		GenerateViewMap();
		if (m_iCursor >= m_ViewMap.GetCount())
			m_iCursor = m_ViewMap.GetCount() - 1;
		}
	}

void CItemListManipulator::DeleteMarkedItems (void)

//	DeleteMarkedItems
//
//	Deletes all items marked for deletion

	{
	for (int i = 0; i < m_ItemList.GetCount(); i++)
		{
		CItem &ThisItem = m_ItemList.GetItem(i);

		if (ThisItem.IsMarkedForDelete())
			{
			m_ItemList.DeleteItem(i);
			i--;
			}
		}
	}

int CItemListManipulator::FindItem (const CItem &Item, DWORD dwFlags) const

//	FindItem
//
//	Finds an item in the list that matches the given one. Returns
//	the index in the list of the item or -1 if not found.

	{
	for (int i = 0; i < m_ViewMap.GetCount(); i++)
		{
		CItem &ThisItem = m_ItemList.GetItem(m_ViewMap[i]);

		if (ThisItem.IsEqual(Item, dwFlags))
			return i;
		}

	return -1;
	}

void CItemListManipulator::GenerateViewMap (void)

//	GenerateViewMap
//
//	Generates a mapping from index to an index into the item list

	{
	m_ViewMap.DeleteAll();

	for (int i = 0; i < m_ItemList.GetCount(); i++)
		{
		CItem &ThisItem = m_ItemList.GetItem(i);

		if (!m_bUseFilter || ThisItem.MatchesCriteria(m_Filter))
			m_ViewMap.Insert(i);
		}
	}

const CItem &CItemListManipulator::GetItemAtCursor (void)

//	GetItemAtCursor
//
//	Returns the item at the cursor

	{
	if (m_iCursor == -1)
		return CItem::NullItem();

	return m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	}

CItem *CItemListManipulator::GetItemPointerAtCursor (void)

//	GetItemPointerAtCursor
//
//	Returns a pointer to the item

	{
	ASSERT(m_iCursor != -1);
	return &m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	}

bool CItemListManipulator::IsItemPointerValid (const CItem *pItem) const

//	IsItemPointerValid
//
//	Returns TRUE if the given item is part of this list.

	{
	int i;

	for (i = 0; i < m_ItemList.GetCount(); i++)
		if (&m_ItemList.GetItem(i) == pItem)
			return true;

	return false;
	}

void CItemListManipulator::MarkDeleteAtCursor (int iCount)

//	MarkDeleteAtCursor
//
//	Deletes a certain number of copies at the cursor. If
//	All the copies of the item are deleted, the entire item
//	is marked for deletion (and is deleted when we call
//	DeleteMarkedItems)

	{
	ASSERT(m_iCursor != -1);

	CItem &ThisItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	if (ThisItem.GetCount() > iCount)
		ThisItem.SetCount(ThisItem.GetCount() - iCount);
	else
		ThisItem.MarkForDelete();
	}

bool CItemListManipulator::MoveCursorBack (void)

//	MoveCursorBack
//
//	Moves the cursor to the previous item. If there are no more item
//	returns false

	{
	if (m_iCursor == -1 && m_ViewMap.GetCount() > 0)
		{
		m_iCursor = m_ViewMap.GetCount() - 1;
		return true;
		}
	else if (m_iCursor <= 0)
		return false;
	else
		{
		m_iCursor--;
		return true;
		}
	}

bool CItemListManipulator::MoveCursorForward (void)

//	MoveCursorForward
//
//	Moves the cursor to the next item. If there are no more items
//	returns false

	{
	if (m_iCursor + 1 == m_ViewMap.GetCount())
		return false;
	else
		{
		m_iCursor++;
		return true;
		}
	}

void CItemListManipulator::MoveItemTo (const CItem &NewItem, const CItem &OldItem)

//	MoveItemTo
//
//	Move NewItem.GetCount() item from OldItem to NewItem, adding and removing items
//	as appropriate. The cursor is left on NewItem

	{
	if (SetCursorAtItem(OldItem))
		{
		//	If new and old items are the same, change in place

		if (OldItem.IsEqual(NewItem))
			{
			//	We don't need to change the original item, since it is identical to
			//	the new item, but we make sure we have at least as many as the new
			//	item.

			if (OldItem.GetCount() < NewItem.GetCount())
				{
				CItem &Item = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
				Item.SetCount(NewItem.GetCount());
				}
			}

		//	If we're replacing all the old items (and we can't coalesce)
		//	then change in place

		else if (OldItem.GetCount() <= NewItem.GetCount()
				&& FindItem(NewItem) == -1)
			{
			CItem &Item = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
			Item = NewItem;
			}

		//	Otherwise, we delete and add

		else
			{
			//	The device mechanism relies on the fact that pointers
			//	to installed items do not change until uninstalled.

			ASSERT(!OldItem.IsInstalled() || !NewItem.IsInstalled());

			DeleteAtCursor(NewItem.GetCount());
			AddItem(NewItem);
			}
		}
	else
		AddItem(NewItem);
	}

bool CItemListManipulator::Refresh (const CItem &Item, DWORD dwFlags)

//	Refresh
//
//	Refreshes the manipulator from the list and selects
//	the given item.
//
//	Returns TRUE if selection succeeded

	{
	if (dwFlags & FLAG_SORT_ITEMS)
		m_ItemList.SortItems();

	GenerateViewMap();

	if (Item.GetType() == NULL)
		{
		ResetCursor();
		return true;
		}

	return SetCursorAtItem(Item);
	}

void CItemListManipulator::RemoveItemEnhancementAtCursor (DWORD dwID, int iCount)

//	RemoveItemEnhancementAtCursor
//
//	Removes the item enhancement by ID

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));

	if (NewItem.RemoveEnhancement(dwID))
		MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::ResetCursor (void)

//	ResetCursor
//
//	Resets the cursor to the beginning

	{
	m_iCursor = -1;
	}

void CItemListManipulator::SetChargesAtCursor (int iCharges, int iCount)

//	SetChargesAtCursor
//
//	Sets the data field

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Max(0, Min(iCount, OldItem.GetCount())));
	NewItem.SetCharges(iCharges);

	MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::SetCountAtCursor (int iCount)

//	SetCountAtCursor
//
//	Sets the number of items

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	if (iCount <= 0)
		DeleteAtCursor(OldItem.GetCount());
	else
		OldItem.SetCount(iCount);
	}

bool CItemListManipulator::SetCursorAtItem (const CItem &Item, DWORD dwFlags)

//	SetCursorAtItem
//
//	Positions the cursor at the given item
//	Return true if the item was found

	{
	int iPos = FindItem(Item, dwFlags);
	if (iPos == -1)
		return false;

	m_iCursor = iPos;
	return true;
	}

void CItemListManipulator::SetDamagedAtCursor (bool bDamaged, int iCount)

//	SetDamagedAtCursor
//
//	Sets the damage flag

	{
	ASSERT(m_iCursor != -1);

	//	Get the item at the cursor. Abort if there is nothing to do

	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (OldItem.IsDamaged() == bDamaged)
		return;

	//	Generate a new item

	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));
	NewItem.SetDamaged(bDamaged);

	MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::SetDataAtCursor (const CString &sAttrib, ICCItem *pData, int iCount)

//	SetDataAtCursor
//
//	Sets data

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));
	NewItem.SetData(sAttrib, pData);

	MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::SetDisruptedAtCursor (DWORD dwDuration, int iCount)

//	SetDisruptedAtCursor
//
//	Sets the disrupted time

	{
	ASSERT(m_iCursor != -1);

	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);

	//	Generate a new item

	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Min(iCount, OldItem.GetCount()));
	NewItem.SetDisrupted(dwDuration);

	MoveItemTo(NewItem, OldItem);
	}

void CItemListManipulator::SetEnhancedAtCursor (bool bEnhanced)

//	SetEnhancedAtCursor
//
//	Sets the enhanced flag

	{
	ASSERT(m_iCursor != -1);

	//	Get the item at the cursor. Abort if there is nothing to do

	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (OldItem.IsEnhanced() == bEnhanced)
		return;

	//	Generate a new item

	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	NewItem.SetCount(1);
	NewItem.SetEnhanced(bEnhanced);

	//	If we've got a single item, and the new item does not exist, then
	//	we can do it in place

	if (OldItem.GetCount() == 1 && FindItem(NewItem) == -1)
		OldItem.SetEnhanced(bEnhanced);

	//	Otherwise, we delete and add

	else
		{
		DeleteAtCursor(1);
		AddItem(NewItem);
		}
	}

void CItemListManipulator::SetFilter (const CItemCriteria &Filter)

//	SetFilter
//
//	Set filter for item list

	{
	m_Filter = Filter;
	m_bUseFilter = true;
	m_iCursor = -1;
	GenerateViewMap();
	}

void CItemListManipulator::SetInstalledAtCursor (int iInstalled)

//	SetInstalledAtCursor
//
//	Installs the selected item

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	NewItem.SetCount(1);
	NewItem.SetInstalled(iInstalled);

	MoveItemTo(NewItem, OldItem);
	}

bool CItemListManipulator::SetPropertyAtCursor (CSpaceObject *pSource, const CString &sName, ICCItem *pValue, int iCount, CString *retsError)

//	SetPropertyAtCursor
//
//	Sets the property

	{
	ASSERT(m_iCursor != -1);
	CItem &OldItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	CItem NewItem = m_ItemList.GetItem(m_ViewMap[m_iCursor]);
	if (iCount != -1)
		NewItem.SetCount(Max(0, Min(iCount, OldItem.GetCount())));

	CItemCtx ItemCtx(&NewItem, pSource);
	if (!NewItem.SetProperty(ItemCtx, sName, pValue, retsError))
		return false;

	MoveItemTo(NewItem, OldItem);

	return true;
	}

void CItemListManipulator::SyncCursor (void)

//	SyncCursor
//
//	Make sure the cursor is in bounds.

	{
	if (m_iCursor != -1
			&& m_iCursor >= m_ItemList.GetCount())
		m_iCursor = m_ItemList.GetCount() - 1;
	}

void CItemListManipulator::TransferAtCursor (int iCount, CItemList &DestList)

//	TransferAtCursor
//
//	Transfers copies from the cursor to the given destination list

	{
	ASSERT(m_iCursor != -1);

	CItemListManipulator DestListManipulator(DestList);
	CItem ThisItem = GetItemAtCursor();
	ThisItem.SetCount(iCount);

	DestListManipulator.AddItem(ThisItem);
	DeleteAtCursor(iCount);
	}
