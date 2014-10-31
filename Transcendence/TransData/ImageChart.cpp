//	ImageChart.cpp
//
//	Generate a poster of type images.
//	Copyright (c) 2013 by Kronosaur Productions, LLC. All Rights Reserved.

#include <stdio.h>

#include <windows.h>
#include <ddraw.h>
#include "Alchemy.h"
#include "XMLUtil.h"
#include "TransData.h"

struct SEntryDesc
	{
	SEntryDesc (void) : pType(NULL),
			iSize(0),
			pImage(NULL),
			iRotation(0)
		{ }

	CDesignType *pType;						//	The type
	CString sName;							//	Name
	int iSize;								//	Size (units depend on design type)
	const CObjectImageArray *pImage;		//	Image
	int iRotation;							//	Used by ships (0 for others)

	CCompositeImageSelector Selector;		//	Used by station types
	};

void GenerateImageChart (CUniverse &Universe, CXMLElement *pCmdLine)
	{
	int i;

	enum OrderTypes
		{
		orderSmallest = 1,
		orderLargest = 2,
		orderName = 3,
		orderLevel = 4,
		};

	//	Item criteria

	bool bHasItemCriteria;
	CString sCriteria;
	CItemCriteria ItemCriteria;
	if (bHasItemCriteria = pCmdLine->FindAttribute(CONSTLIT("itemCriteria"), &sCriteria))
		CItem::ParseCriteria(sCriteria, &ItemCriteria);
	else
		CItem::InitCriteriaAll(&ItemCriteria);

	//	Get the criteria from the command line.

	CDesignTypeCriteria Criteria;
	if (pCmdLine->FindAttribute(CONSTLIT("criteria"), &sCriteria))
		{
		if (CDesignTypeCriteria::ParseCriteria(sCriteria, &Criteria) != NOERROR)
			{
			printf("ERROR: Unable to parse criteria.\n");
			return;
			}
		}
	else if (bHasItemCriteria)
		{
		if (CDesignTypeCriteria::ParseCriteria(CONSTLIT("i"), &Criteria) != NOERROR)
			{
			printf("ERROR: Unable to parse criteria.\n");
			return;
			}
		}
	else
		{
		printf("ERROR: Expected criteria.\n");
		return;
		}

	bool bAll = pCmdLine->GetAttributeBool(CONSTLIT("all"));

	//	Options

	bool bTextBoxesOnly = pCmdLine->GetAttributeBool(CONSTLIT("textBoxesOnly"));
	bool bFieldUNID = pCmdLine->GetAttributeBool(CONSTLIT("unid"));

	//	Figure out what order we want

	CString sOrder = pCmdLine->GetAttribute(CONSTLIT("sort"));
	int iOrder;
	if (strEquals(sOrder, CONSTLIT("smallest")))
		iOrder = orderSmallest;
	else if (strEquals(sOrder, CONSTLIT("largest")))
		iOrder = orderLargest;
	else if (strEquals(sOrder, CONSTLIT("level")))
		iOrder = orderLevel;
	else
		iOrder = orderName;

	bool bDockingPorts = pCmdLine->GetAttributeBool(CONSTLIT("portPos"));
	bool bDevicePos = pCmdLine->GetAttributeBool(CONSTLIT("devicePos"));

	//	Image size

	int cxDesiredWidth;
	if (pCmdLine->FindAttributeInteger(CONSTLIT("width"), &cxDesiredWidth))
		cxDesiredWidth = Max(512, cxDesiredWidth);
	else
		cxDesiredWidth = 1280;

	//	Spacing

	int cxSpacing = pCmdLine->GetAttributeInteger(CONSTLIT("xSpacing"));
	int cxExtraMargin = pCmdLine->GetAttributeInteger(CONSTLIT("xMargin"));

	//	Font for text

	CString sTypeface;
	int iSize;
	bool bBold;
	bool bItalic;

	if (!CG16bitFont::ParseFontDesc(pCmdLine->GetAttribute(CONSTLIT("font")),
			&sTypeface,
			&iSize,
			&bBold,
			&bItalic))
		{
		sTypeface = CONSTLIT("Arial");
		iSize = 10;
		bBold = false;
		bItalic = false;
		}

	CG16bitFont NameFont;
	NameFont.Create(sTypeface, -PointsToPixels(iSize), bBold, bItalic);
	WORD wNameColor = CG16bitImage::RGBValue(255, 255, 255);

	//	Rotation

	int iRotation = pCmdLine->GetAttributeInteger(CONSTLIT("rotation"));

	//	Output file

	CString sFilespec = pCmdLine->GetAttribute(CONSTLIT("output"));
	if (!sFilespec.IsBlank())
		sFilespec = pathAddExtensionIfNecessary(sFilespec, CONSTLIT(".bmp"));

	//	Generate a sorted table of types

	TSortMap<CString, SEntryDesc> Table;
	for (i = 0; i < Universe.GetDesignTypeCount(); i++)
		{
		CDesignType *pType = Universe.GetDesignType(i);
		SEntryDesc NewEntry;

		//	Make sure we match the criteria

		if (!pType->MatchesCriteria(Criteria))
			continue;

		//	Figure stuff stuff out based on the specific design type

		switch (pType->GetType())
			{
			case designItemType:
				{
				CItemType *pItemType = CItemType::AsType(pType);
				CItem Item(pItemType, 1);

				//	Skip if not in item criteria

				if (!Item.MatchesCriteria(ItemCriteria))
					continue;

				//	Skip virtual classes

				if (pItemType->IsVirtual())
					continue;

				//	Initialize the entry

				NewEntry.pType = pType;
				NewEntry.sName = pItemType->GetNounPhrase(0);
				NewEntry.pImage = &pItemType->GetImage();
				NewEntry.iSize = RectWidth(NewEntry.pImage->GetImageRect());
				break;
				}

			case designShipClass:
				{
				CShipClass *pClass = CShipClass::AsType(pType);

				//	Skip player classes, unless we want all.

				if (!bAll && pClass->IsPlayerShip())
					continue;

				//	Skip non-generic classess

				if (!bAll && !pClass->HasLiteralAttribute(CONSTLIT("genericClass")))
					continue;

				//	Initialize the entry

				NewEntry.pType = pType;
				NewEntry.sName = pClass->GetNounPhrase(0);
				NewEntry.iSize = RectWidth(pClass->GetImage().GetImageRect());
				NewEntry.pImage = &pClass->GetImage();
				NewEntry.iRotation = pClass->Angle2Direction(iRotation);

				break;
				}

			case designStationType:
				{
				CStationType *pStationType = CStationType::AsType(pType);

				//	Skip generic classes

				if (!bAll && !pStationType->HasLiteralAttribute(CONSTLIT("generic")))
					continue;

				NewEntry.pType = pType;
				NewEntry.sName = pStationType->GetNounPhrase(0);
				NewEntry.iSize = pStationType->GetSize();

				SSelectorInitCtx InitCtx;
				pStationType->SetImageSelector(InitCtx, &NewEntry.Selector);
				NewEntry.pImage = &pStationType->GetImage(NewEntry.Selector, CCompositeImageModifiers());
				break;
				}

			default:
				//	Don't know how to handle this type
				continue;
				break;
			}

		//	Adjust name

		if (bFieldUNID)
			NewEntry.sName = strPatternSubst(CONSTLIT("%s (%x)"), NewEntry.sName, NewEntry.pType->GetUNID());

		//	Compute the sort key

		char szBuffer[1024];
		switch (iOrder)
			{
			case orderLargest:
				wsprintf(szBuffer, "%09d%s%x",
						1000000 - NewEntry.iSize,
						NewEntry.sName.GetASCIIZPointer(),
						pType->GetUNID());
				break;

			case orderLevel:
				wsprintf(szBuffer, "%09d%s%x",
						pType->GetLevel(),
						NewEntry.sName.GetASCIIZPointer(),
						pType->GetUNID());
				break;

			case orderSmallest:
				wsprintf(szBuffer, "%09d%s%x",
						NewEntry.iSize,
						NewEntry.sName.GetASCIIZPointer(),
						pType->GetUNID());
				break;

			default:
				wsprintf(szBuffer, "%s%x", NewEntry.sName.GetASCIIZPointer(), pType->GetUNID());
				break;
			}

		//	Add to list

		Table.Insert(CString(szBuffer), NewEntry);
		}

	//	Allocate an arranger that tracks where to paint each world.

	CImageArranger Arranger;

	//	Settings for the overall arrangement

	CImageArranger::SArrangeDesc Desc;
	Desc.cxDesiredWidth = Max(512, cxDesiredWidth - (2 * (cxSpacing + cxExtraMargin)));
	Desc.cxSpacing = cxSpacing;
	Desc.cxExtraMargin = cxExtraMargin;
	Desc.pHeader = &NameFont;

	//	Generate a table of cells for the arranger

	TArray<CCompositeImageSelector> Selectors;
	Selectors.InsertEmpty(Table.GetCount());

	TArray<CImageArranger::SCellDesc> Cells;
	for (i = 0; i < Table.GetCount(); i++)
		{
		SEntryDesc &Entry = Table[i];

		CImageArranger::SCellDesc *pNewCell = Cells.Insert();
		pNewCell->cxWidth = (Entry.pImage ? RectWidth(Entry.pImage->GetImageRect()) : 0);
		pNewCell->cyHeight = (Entry.pImage ? RectHeight(Entry.pImage->GetImageRect()) : 0);
		pNewCell->sText = Entry.sName;
		}

	//	Arrange

	Arranger.ArrangeByRow(Desc, Cells);

	//	Create a large image

	CG16bitImage Output;
	int cxWidth = Max(cxDesiredWidth, Arranger.GetWidth());
	int cyHeight = Arranger.GetHeight();
	Output.CreateBlank(cxWidth, cyHeight, false);
	printf("Creating %dx%d image.\n", cxWidth, cyHeight);

	//	Paint the images

	for (i = 0; i < Table.GetCount(); i++)
		{
		SEntryDesc &Entry = Table[i];

		int x = Arranger.GetX(i);
		int y = Arranger.GetY(i);

		//	Paint

		if (x != -1)
			{
			int xCenter = x + (Arranger.GetWidth(i) / 2);
			int yCenter = y + (Arranger.GetHeight(i) / 2);

			int xOffset;
			int yOffset;
			Entry.pImage->GetImageOffset(0, Entry.iRotation, &xOffset, &yOffset);

			//	Paint image

			if (!bTextBoxesOnly && Entry.pImage)
				{
				Entry.pImage->PaintImageUL(Output,
						x,
						y,
						0,
						Entry.iRotation);
				}

			//	Paint type specific stuff

			switch (Entry.pType->GetType())
				{
				case designStationType:
					{
					CStationType *pStationType = CStationType::AsType(Entry.pType);
					if (bDockingPorts)
						pStationType->PaintDockPortPositions(Output, xCenter - xOffset, yCenter - yOffset);
					break;
					}
				}

			//	Paint name

			int xText = Arranger.GetTextX(i);
			int yText = Arranger.GetTextY(i);
			if (xText != -1)
				{
				if (bTextBoxesOnly)
					Output.Fill(xText, yText, Arranger.GetTextWidth(i), Arranger.GetTextHeight(i), 0xffff);

				if (!bTextBoxesOnly)
					{
					Output.FillColumn(xCenter,
							y + Arranger.GetHeight(i),
							yText - (y + Arranger.GetHeight(i)),
							wNameColor);

					NameFont.DrawText(Output,
							xText,
							yText,
							wNameColor,
							255,
							Entry.sName);
					}
				}
			}
		}

	//	Write to file or clipboard

	OutputImage(Output, sFilespec);
	}

