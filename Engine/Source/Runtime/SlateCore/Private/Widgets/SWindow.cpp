// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#include "SlateCorePrivatePCH.h"
#include "HittestGrid.h"

namespace SWindowDefs
{
	/** Height of a Slate window title bar, in pixels */
	static const float DefaultTitleBarSize = 24.0f;

	/** Size of the corner rounding radius.  Used for regular, non-maximized windows only (not tool-tips or decorators.) */
	static const int32 CornerRadius = 6;
}


/**
 * An internal overlay used by Slate to support in-window pop ups and tooltips.
 * The overlay ignores DPI scaling when it does its own arrangement, but otherwise
 * passes all DPI scale values through.
 */
class SPopupLayer : public SPanel
{
public:
	SLATE_BEGIN_ARGS( SPopupLayer )
		{
			_Visibility = EVisibility::SelfHitTestInvisible;
		}

		SLATE_SUPPORTS_SLOT( FPopupLayerSlot )

	SLATE_END_ARGS()

	SPopupLayer()
	: Children()
	{}

	void Construct( const FArguments& InArgs, const TSharedRef<SWindow>& InWindow )
	{
		OwnerWindow = InWindow;

		const int32 NumSlots = InArgs.Slots.Num();
		for ( int32 SlotIndex = 0; SlotIndex < NumSlots; ++SlotIndex )
		{
			Children.Add( InArgs.Slots[SlotIndex] );
		}
	}

	/** Make a new ListPanel::Slot  */
	FPopupLayerSlot& Slot()
	{
		return *(new FPopupLayerSlot());
	}
	
	/** Add a slot to the ListPanel */
	FPopupLayerSlot& AddSlot(int32 InsertAtIndex = INDEX_NONE)
	{
		FPopupLayerSlot& NewSlot = *new FPopupLayerSlot();
		if (InsertAtIndex == INDEX_NONE)
		{
			this->Children.Add( &NewSlot );
		}
		else
		{
			this->Children.Insert( &NewSlot, InsertAtIndex );
		}
	
		return NewSlot;
	}

	void RemoveSlot(const TSharedRef<SWidget>& WidgetToRemove)
	{
		for( int32 CurSlotIndex = 0; CurSlotIndex < Children.Num(); ++CurSlotIndex )
		{
			const FPopupLayerSlot& CurSlot = Children[ CurSlotIndex ];
			if( CurSlot.GetWidget() == WidgetToRemove )
			{
				Children.RemoveAt( CurSlotIndex );
				return;
			}
		}
	}

private:

	/**
	 * Each child slot essentially tries to place their contents at a specified position on the screen
	 * and scale as the widget initiating the popup, both of which are stored in the slot attributes.
	 * The tricky part is that the scale we are given is the fully accumulated layout scale of the widget, which already incorporates 
	 * the DPI Scale of the window. The DPI Scale is also applied to the overlay since it is part of the window,
	 * so this scale needs to be factored out when determining the scale of the child geometry that will be created to hold the popup.
	 * We also optionally adjust the window position to keep it within the client bounds of the top-level window. This must be done in screenspace.
	 * This means some hairy transformation calculus goes on to ensure the computations are done in the proper space so scale is respected.
	 * 
	 * There are 3 transformational spaces involved, each clearly specified in the variable names:
	 * Screen      - Basically desktop space. Contains desktop offset and DPI scale.
	 * WindowLocal - local space of the SWindow containing this popup. Screenspace == Concat(WindowLocal, DPI Scale, Desktop Offset)
	 * ChildLocal  - space of the child widget we want to display in the popup. The widget's LayoutTransform takes us from ChildLocal to WindowLocal space.
	 */
	virtual void OnArrangeChildren( const FGeometry& AllottedGeometry, FArrangedChildren& ArrangedChildren ) const override
	{
		// skip all this work if there are no children to arrange.
		if (Children.Num() == 0) return;

		// create a transform from screen to local space.
		// This assumes that the PopupLayer is part of an Overlay that takes up the entire window space.
		// We should technically be using the AllottedGeometry to transform from AbsoluteToLocal space just in case it has an additional scale on it.
		// But we can't because the absolute space of the geometry is sometimes given in desktop space (picking, ticking) 
		// and sometimes in window space (painting), and we can't necessarily tell by inspection so we have to just make an assumption here.
		FSlateLayoutTransform ScreenToWindowLocal = (ensure(OwnerWindow.IsValid())) ? Inverse(OwnerWindow.Pin()->GetLocalToScreenTransform()) : FSlateLayoutTransform();
		
		for ( int32 ChildIndex = 0; ChildIndex < Children.Num(); ++ChildIndex )
		{
			const FPopupLayerSlot& CurChild = Children[ChildIndex];
			const EVisibility ChildVisibility = CurChild.GetWidget()->GetVisibility();
			if ( ArrangedChildren.Accepts(ChildVisibility) )
			{
				// This scale+translate forms the ChildLocal to Screenspace transform.
				// The translation may be adjusted based on clamping, but the scale is accurate, 
				// so we can transform vectors into screenspace using the scale alone.
				const float ChildLocalToScreenScale = CurChild.Scale_Attribute.Get();
				FVector2D ChildLocalToScreenOffset = CurChild.DesktopPosition_Attribute.Get();
				// The size of the child is either the desired size of the widget (computed in the child's local space) or the size override (specified in screen space)
				const FVector2D ChildSizeChildLocal = CurChild.GetWidget()->GetDesiredSize();
				// Convert the desired size to screen space. Here is were we convert a vector to screenspace
				// before we have the final position in screenspace (which would be needed to transform a point).
				FVector2D ChildSizeScreenspace = TransformVector(ChildLocalToScreenScale, ChildSizeChildLocal);
				// But then allow each size dimension to be overridden by the slot, which specifies the overrides in screen space.
				ChildSizeScreenspace = FVector2D(
						CurChild.WidthOverride_Attribute.IsSet() ? CurChild.WidthOverride_Attribute.Get() : ChildSizeScreenspace.X,
						CurChild.HeightOverride_Attribute.IsSet() ? CurChild.HeightOverride_Attribute.Get() : ChildSizeScreenspace.Y);
				
				// If clamping, move the screen space position to ensure the screen space size stays within the client rect of the top level window.
				if(CurChild.Clamp_Attribute.Get())
				{
					const FSlateRect WindowClientRectScreenspace = (ensure(OwnerWindow.IsValid())) ? OwnerWindow.Pin()->GetClientRectInScreen() : FSlateRect();
					const FVector2D ClampBufferScreenspace = CurChild.ClampBuffer_Attribute.Get();
					const FSlateRect ClampedWindowClientRectScreenspace = WindowClientRectScreenspace.InsetBy(FMargin(ClampBufferScreenspace.X, ClampBufferScreenspace.Y));
					// Find how much our child wants to extend beyond our client space and subtract that amount, but don't push it past the client edge.
					ChildLocalToScreenOffset.X = FMath::Max(WindowClientRectScreenspace.Left, ChildLocalToScreenOffset.X - FMath::Max(0.0f, (ChildLocalToScreenOffset.X  + ChildSizeScreenspace.X ) - ClampedWindowClientRectScreenspace.Right));
					ChildLocalToScreenOffset.Y = FMath::Max(WindowClientRectScreenspace.Top, ChildLocalToScreenOffset.Y - FMath::Max(0.0f, (ChildLocalToScreenOffset.Y  + ChildSizeScreenspace.Y ) - ClampedWindowClientRectScreenspace.Bottom));
				}

				// We now have the final position, so construct the transform from ChildLocal to Screenspace
				const FSlateLayoutTransform ChildLocalToScreen(ChildLocalToScreenScale, ChildLocalToScreenOffset);
				// Using this we can compute the transform from ChildLocal to WindowLocal, which is effectively the LayoutTransform of the child widget.
				const FSlateLayoutTransform ChildLocalToWindowLocal = Concatenate(ChildLocalToScreen, ScreenToWindowLocal);
				// The ChildSize needs to be given in ChildLocal space when constructing a geometry.
				const FVector2D ChildSizeLocalspace = TransformVector(Inverse(ChildLocalToScreen), ChildSizeScreenspace);

				// The position is explicitly in desktop pixels.
				// The size and DPI scale come from the widget that is using
				// this overlay to "punch" through the UI.
				ArrangedChildren.AddWidget(AllottedGeometry.MakeChild(CurChild.GetWidget(), ChildSizeLocalspace, ChildLocalToWindowLocal));
			}
		}
	}

	virtual FVector2D ComputeDesiredSize() const override
	{
		return FVector2D(100,100);
	}

	/**
	 * All widgets must provide a way to access their children in a layout-agnostic way.
	 * Panels store their children in Slots, which creates a dilemma. Most panels
	 * can store their children in a TPanelChildren<Slot>, where the Slot class
	 * provides layout information about the child it stores. In that case
	 * GetChildren should simply return the TPanelChildren<Slot>. See StackPanel for an example.
	 */
	virtual FChildren* GetChildren() override
	{
		return &Children;
	}

	TPanelChildren<FPopupLayerSlot> Children;
	TWeakPtr<SWindow> OwnerWindow;
};


void SWindow::Construct(const FArguments& InArgs)
{
	check(InArgs._Style);
	this->Style = InArgs._Style;
	this->WindowBackground = &InArgs._Style->BackgroundBrush;

	this->Title = InArgs._Title;
	this->bDragAnywhere = InArgs._bDragAnywhere;
	this->bIsTransparent = InArgs._SupportsTransparency;
	this->Opacity = InArgs._InitialOpacity;
	this->bInitiallyMaximized = InArgs._IsInitiallyMaximized;
	this->SizingRule = InArgs._SizingRule;
	this->bIsPopupWindow = InArgs._IsPopupWindow;
	this->bFocusWhenFirstShown = InArgs._FocusWhenFirstShown;
	this->bActivateWhenFirstShown = InArgs._ActivateWhenFirstShown;
	this->bHasOSWindowBorder = InArgs._UseOSWindowBorder;
	this->bHasMinimizeButton = InArgs._SupportsMinimize;
	this->bHasMaximizeButton = InArgs._SupportsMaximize;
	this->bHasSizingFrame = !InArgs._IsPopupWindow && InArgs._SizingRule == ESizingRule::UserSized;
	this->LayoutBorder = InArgs._LayoutBorder;
	this->UserResizeBorder = InArgs._UserResizeBorder;
	this->SizeLimits = FWindowSizeLimits()
		.SetMinWidth(InArgs._MinWidth)
		.SetMinHeight(InArgs._MinHeight)
		.SetMaxWidth(InArgs._MaxWidth)
		.SetMaxHeight(InArgs._MaxHeight);
	
	// calculate window size from client size
	const bool bCreateTitleBar = InArgs._CreateTitleBar && !bIsPopupWindow && !bIsCursorDecoratorWindow && !bHasOSWindowBorder;
	FVector2D WindowSize = InArgs._ClientSize;

	// If the window has no OS border, simulate it ourselves, enlarging window by the size that OS border would have.
	if (!HasOSWindowBorder())
	{
		const FMargin BorderSize = GetWindowBorderSize();

		WindowSize.X += BorderSize.Left + BorderSize.Right;
		WindowSize.Y += BorderSize.Bottom + BorderSize.Top;

		if (bCreateTitleBar)
		{
			WindowSize.Y += SWindowDefs::DefaultTitleBarSize;
		}
	}

	// calculate initial window position
	FVector2D WindowPosition = InArgs._ScreenPosition;

	AutoCenterRule = InArgs._AutoCenter;

	// Get desktop metrics
	FDisplayMetrics DisplayMetrics;
	FSlateApplicationBase::Get().GetDisplayMetrics( DisplayMetrics );
	const FPlatformRect& VirtualDisplayRect = DisplayMetrics.VirtualDisplayRect;
	const FPlatformRect& PrimaryDisplayRect = DisplayMetrics.PrimaryDisplayWorkAreaRect;

	// If we're manually positioning the window we need to check if it's outside
	// of the virtual bounds of the current displays or too large.
	if ( AutoCenterRule == EAutoCenter::None && InArgs._SaneWindowPlacement )
	{
		// Check to see if the upper left corner of the window is outside the virtual
		// bounds of the display, if so reset to preferred work area
		if (WindowPosition.X < VirtualDisplayRect.Left ||
			WindowPosition.X >= VirtualDisplayRect.Right ||
			WindowPosition.Y < VirtualDisplayRect.Top ||
			WindowPosition.Y >= VirtualDisplayRect.Bottom)
		{
			AutoCenterRule = EAutoCenter::PreferredWorkArea;
		}

		float PrimaryWidthPadding = DisplayMetrics.PrimaryDisplayWidth - 
			(PrimaryDisplayRect.Right - PrimaryDisplayRect.Left);
		float PrimaryHeightPadding = DisplayMetrics.PrimaryDisplayHeight - 
			(PrimaryDisplayRect.Bottom - PrimaryDisplayRect.Top);

		float VirtualWidth = (VirtualDisplayRect.Right - VirtualDisplayRect.Left);
		float VirtualHeight = (VirtualDisplayRect.Bottom - VirtualDisplayRect.Top);

		// Make sure that the window size is no larger than the virtual display area.
		WindowSize.X = FMath::Clamp(WindowSize.X, 0.0f, VirtualWidth - PrimaryWidthPadding);
		WindowSize.Y = FMath::Clamp(WindowSize.Y, 0.0f, VirtualHeight - PrimaryHeightPadding);
	}

	if( AutoCenterRule != EAutoCenter::None )
	{
		FSlateRect AutoCenterRect;

		switch( AutoCenterRule )
		{
		default:
		case EAutoCenter::PrimaryWorkArea:
			AutoCenterRect = FSlateRect(
				(float)PrimaryDisplayRect.Left, 
				(float)PrimaryDisplayRect.Top,
				(float)PrimaryDisplayRect.Right,
				(float)PrimaryDisplayRect.Bottom );		
			break;
		case EAutoCenter::PreferredWorkArea:
			AutoCenterRect = FSlateApplicationBase::Get().GetPreferredWorkArea();
			break;
		}

		// Setup a position and size for the main frame window that's centered in the desktop work area
		const FVector2D DisplayTopLeft( AutoCenterRect.Left, AutoCenterRect.Top );
		const FVector2D DisplaySize( AutoCenterRect.Right - AutoCenterRect.Left, AutoCenterRect.Bottom - AutoCenterRect.Top );
		WindowPosition = DisplayTopLeft + ( DisplaySize - WindowSize ) * 0.5f;
	}

#if PLATFORM_HTML5 
	// UE expects mouse coordinates in screen space. SDL/HTML5 canvas provides in client space. 
	// Anchor the window at the top/left corner to make sure client space coordinates and screen space coordinates match up. 
	WindowPosition.X =  WindowPosition.Y = 0; 
#endif 
	this->InitialDesiredScreenPosition = WindowPosition;
	this->InitialDesiredSize = WindowSize;

	// Window visibility is currently driven by whether the window is interactive.
	this->Visibility = TAttribute<EVisibility>::Create( TAttribute<EVisibility>::FGetter::CreateRaw(this, &SWindow::GetWindowVisibility) );

	this->ConstructWindowInternals( bCreateTitleBar );
	this->SetContent( InArgs._Content.Widget );
}


TSharedRef<SWindow> SWindow::MakeNotificationWindow()
{
	TSharedRef<SWindow> NewWindow =
		SNew(SWindow)
		.SupportsMaximize( false )
		.SupportsMinimize( false )
		.IsPopupWindow( true )
		.CreateTitleBar( false )
		.SizingRule( ESizingRule::Autosized )
		.SupportsTransparency( true )
		.InitialOpacity( 0.0f )
		.FocusWhenFirstShown( false )
		.ActivateWhenFirstShown( false );

	// Notification windows slide open so we'll mark them as resized frequently
	NewWindow->bSizeWillChangeOften = true;
	NewWindow->ExpectedMaxWidth = 1024;
	NewWindow->ExpectedMaxHeight = 256;

	return NewWindow;
}


TSharedRef<SWindow> SWindow::MakeToolTipWindow()
{
	TSharedRef<SWindow> NewWindow = SNew( SWindow )
		.IsPopupWindow( true )
		.SizingRule( ESizingRule::Autosized )
		.FocusWhenFirstShown( false )
		.ActivateWhenFirstShown( false );
	NewWindow->bIsToolTipWindow = true;
	NewWindow->bIsTopmostWindow = true;
	NewWindow->bIsTransparent = true;
	NewWindow->Opacity = 0.0f;

	// NOTE: These sizes are tweaked for SToolTip widgets (text wrap width of around 400 px)
	NewWindow->bSizeWillChangeOften = true;
	NewWindow->ExpectedMaxWidth = 512;
	NewWindow->ExpectedMaxHeight = 256;

	return NewWindow;
}


TSharedRef<SWindow> SWindow::MakeCursorDecorator()
{
	TSharedRef<SWindow> NewWindow = SNew( SWindow )
		.IsPopupWindow( true )
		.SizingRule( ESizingRule::Autosized )
		.FocusWhenFirstShown( false )
		.ActivateWhenFirstShown( false );
	NewWindow->bIsToolTipWindow = true;
	NewWindow->bIsTopmostWindow = true;
	NewWindow->bIsCursorDecoratorWindow = true;
	NewWindow->bIsTransparent = true;
	NewWindow->Opacity = 1.0f;

	return NewWindow;
}

FVector2D SWindow::ComputeWindowSizeForContent( FVector2D ContentSize )
{
	// @todo mainframe: This code should be updated to handle the case where we're spawning a window that doesn't have 
	//                  a traditional title bar, such as a window that contains a primary SDockingArea.  Currently, the
	//                  size reported here will be too large!
	return ContentSize + FVector2D(0, SWindowDefs::DefaultTitleBarSize);
}

void SWindow::ConstructWindowInternals( const bool bCreateTitleBar )
{
	ForegroundColor = FCoreStyle::Get().GetSlateColor("DefaultForeground");

	// Setup widget that represents the main area of the window.  That is, everything inside the window's border.
	TSharedRef< SVerticalBox > MainWindowArea = 
		SNew( SVerticalBox )
			.Visibility( EVisibility::SelfHitTestInvisible );

	if (bCreateTitleBar)
	{
		// @todo mainframe: Should be measured from actual title bar content widgets.  Don't use a hard-coded size!
		TitleBarSize = SWindowDefs::DefaultTitleBarSize;

		EWindowTitleAlignment::Type TitleAlignment = FSlateApplicationBase::Get().GetPlatformApplication()->GetWindowTitleAlignment();
		EHorizontalAlignment TitleContentAlignment;

		if (TitleAlignment == EWindowTitleAlignment::Left)
		{
			TitleContentAlignment = HAlign_Left;
		}
		else if (TitleAlignment == EWindowTitleAlignment::Center)
		{
			TitleContentAlignment = HAlign_Center;
		}
		else
		{
			TitleContentAlignment = HAlign_Right;
		}

		MainWindowArea->AddSlot()
			.AutoHeight()
			[
				FSlateApplicationBase::Get().MakeWindowTitleBar(SharedThis(this), nullptr, TitleContentAlignment, TitleBar)
			];
	}
	else
	{
		TitleBarSize = 0;
	}

	// create window content slot
	MainWindowArea->AddSlot()
		.FillHeight(1.0f)
		.Expose(ContentSlot)
		[
			SNullWidget::NullWidget
		];

	// create window
	if (!bIsToolTipWindow && !bIsPopupWindow && !bHasOSWindowBorder)
	{
		TAttribute<EVisibility> WindowContentVisibility(this, &SWindow::GetWindowContentVisibility);
		TAttribute<const FSlateBrush*> WindowBackgroundAttr(this, &SWindow::GetWindowBackground);
		TAttribute<const FSlateBrush*> WindowOutlineAttr(this, &SWindow::GetWindowOutline);
		TAttribute<FSlateColor> WindowOutlineColorAttr(this, &SWindow::GetWindowOutlineColor);

		this->ChildSlot
		[
			SAssignNew(WindowOverlay, SOverlay)
				.Visibility(EVisibility::SelfHitTestInvisible)

			// window background
			+ SOverlay::Slot()
				[
					FSlateApplicationBase::Get().MakeImage(
						WindowBackgroundAttr,
						FLinearColor::White,
						WindowContentVisibility
					)
				]

			// window border
			+ SOverlay::Slot()
				[
					FSlateApplicationBase::Get().MakeImage(
						&Style->BorderBrush,
						FLinearColor::White,
						WindowContentVisibility
					)
				]

			// main area
			+ SOverlay::Slot()
				[
					SNew(SVerticalBox)
						.Visibility(WindowContentVisibility)

					+ SVerticalBox::Slot()
					
						.Padding(TAttribute<FMargin>::Create(TAttribute<FMargin>::FGetter::CreateSP(this, &SWindow::GetWindowBorderSize, false)))
						[
							MainWindowArea
						]
				]

			// pop-up layer
			+ SOverlay::Slot()
				[
					SAssignNew(PopupLayer, SPopupLayer, SharedThis(this))
				]

			// window outline
			+ SOverlay::Slot()
				[
					FSlateApplicationBase::Get().MakeImage(
						WindowOutlineAttr,
						WindowOutlineColorAttr,
						WindowContentVisibility
					)
				]
		];
	}
	else if( bHasOSWindowBorder )
	{
		this->ChildSlot
		[
			SAssignNew(WindowOverlay, SOverlay)
			+ SOverlay::Slot()
			[
					MainWindowArea
			]
			+ SOverlay::Slot()
			[
				SAssignNew(PopupLayer, SPopupLayer, SharedThis(this))
			]
		];
	}
}


/** Are any of our child windows active? */
bool SWindow::HasActiveChildren() const
{
	for (int32 i = 0; i < ChildWindows.Num(); ++i)
	{
		if ( ChildWindows[i] == FSlateApplicationBase::Get().GetActiveTopLevelWindow() || ChildWindows[i]->HasActiveChildren() )
		{
			return true;
		}
	}

	return false;
}

TSharedRef<FHittestGrid> SWindow::GetHittestGrid()
{
	return HittestGrid;
}

FWindowSizeLimits SWindow::GetSizeLimits() const
{
	return SizeLimits;
}

void SWindow::Tick( const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime )
{
	if( Morpher.bIsActive )
	{
		if(Morpher.bIsPendingPlay)
		{
			Morpher.Sequence.Play();
			Morpher.bIsPendingPlay = false;
		}
		if ( Morpher.Sequence.IsPlaying() )
		{
			const float InterpAlpha = Morpher.Sequence.GetLerp();

			if( Morpher.bIsAnimatingWindowSize )
			{
				FSlateRect WindowRect = FMath::Lerp( Morpher.StartingMorphShape, Morpher.TargetMorphShape, InterpAlpha );
				if( WindowRect != GetRectInScreen() )
				{
					check( SizingRule != ESizingRule::Autosized );
					this->ReshapeWindow( WindowRect );
				}
			}
			else
			{
				const FVector2D StartPosition( Morpher.StartingMorphShape.Left, Morpher.StartingMorphShape.Top );
				const FVector2D TargetPosition( Morpher.TargetMorphShape.Left, Morpher.TargetMorphShape.Top );
				const FVector2D NewPosition( FMath::Lerp( StartPosition, TargetPosition, InterpAlpha ) );
				if( NewPosition != this->GetPositionInScreen() )
				{
					this->MoveWindowTo( NewPosition );
				}
			}

			const float NewOpacity = FMath::Lerp( Morpher.StartingOpacity, Morpher.TargetOpacity, InterpAlpha );
			this->SetOpacity( NewOpacity );
		}
		else
		{
			if( Morpher.bIsAnimatingWindowSize )
			{
				if( Morpher.TargetMorphShape != GetRectInScreen() )
				{
					check( SizingRule != ESizingRule::Autosized );
					this->ReshapeWindow( Morpher.TargetMorphShape );
				}
			}
			else
			{
				const FVector2D TargetPosition( Morpher.TargetMorphShape.Left, Morpher.TargetMorphShape.Top );
				if( TargetPosition != this->GetPositionInScreen() )
				{
					this->MoveWindowTo( TargetPosition );
				}
			}

			this->SetOpacity( Morpher.TargetOpacity );
			Morpher.bIsActive = false;
		}
	}
}

FVector2D SWindow::GetInitialDesiredSizeInScreen() const
{
	return InitialDesiredSize;
}

FVector2D SWindow::GetInitialDesiredPositionInScreen() const
{
	return InitialDesiredScreenPosition;
}

FGeometry SWindow::GetWindowGeometryInScreen() const
{
	const float AppScale = FSlateApplicationBase::Get().GetApplicationScale();
	// We are scaling children for layout, but our pixel bounds are not changing. 
	// FGeometry expects Size in Local space, but our size is stored in screen space.
	// So we need to transform Size into the window's local space for FGeometry.
	FSlateLayoutTransform LocalToScreen = GetLocalToScreenTransform();
	return FGeometry::MakeRoot( TransformVector(Inverse(LocalToScreen), Size), LocalToScreen );
}

FGeometry SWindow::GetWindowGeometryInWindow() const
{
	const float AppScale = FSlateApplicationBase::Get().GetApplicationScale();
	// We are scaling children for layout, but our pixel bounds are not changing. 
	// FGeometry expects Size in Local space, but our size is stored in screen space (same as window space + screen offset).
	// So we need to transform Size into the window's local space for FGeometry.
	FSlateLayoutTransform LocalToWindow = GetLocalToWindowTransform();
	FVector2D Size = GetViewportSize();
	return FGeometry::MakeRoot( TransformVector(Inverse(LocalToWindow), Size), LocalToWindow );
}

FSlateLayoutTransform SWindow::GetLocalToScreenTransform() const
{
	return FSlateLayoutTransform(FSlateApplicationBase::Get().GetApplicationScale(), ScreenPosition);
}

FSlateLayoutTransform SWindow::GetLocalToWindowTransform() const
{
	return FSlateLayoutTransform(FSlateApplicationBase::Get().GetApplicationScale());
}


FVector2D SWindow::GetPositionInScreen() const
{
	return ScreenPosition;
}

FVector2D SWindow::GetSizeInScreen() const
{
	return Size;
}

FSlateRect SWindow::GetNonMaximizedRectInScreen() const
{
	int X = 0;
	int Y = 0;
	int Width = 0;
	int Height = 0;
	
	if ( NativeWindow->GetRestoredDimensions(X, Y, Width, Height) )
	{
		return FSlateRect( X, Y, X+Width, Y+Height );
	}
	else
	{
		return GetRectInScreen();
	}
}

FSlateRect SWindow::GetRectInScreen() const
{ 
	return FSlateRect( ScreenPosition, ScreenPosition + Size );
}

FSlateRect SWindow::GetClientRectInScreen() const
{
	if (HasOSWindowBorder())
	{
		return GetRectInScreen();
	}

	return GetRectInScreen()
		.InsetBy(GetWindowBorderSize())
		.InsetBy(FMargin(0.0f, TitleBarSize, 0.0f, 0.0f));
}

FVector2D SWindow::GetClientSizeInScreen() const
{
	return GetClientRectInScreen().GetSize();
}

FSlateRect SWindow::GetClippingRectangleInWindow() const
{
	FVector2D Size = GetViewportSize();
	return FSlateRect( 0, 0, Size.X, Size.Y );
}


FMargin SWindow::GetWindowBorderSize( bool bIncTitleBar ) const
{
// Mac didn't want a window border, and consoles don't either, so only do this in Windows

// @TODO This is not working for Linux. The window is not yet valid when this gets
// called from SWindow::Construct which is causing a default border to be retured even when the
// window is borderless. This causes problems for menu positioning.
	if (NativeWindow.IsValid() && NativeWindow->IsMaximized())
	{
		FMargin BorderSize(NativeWindow->GetWindowBorderSize());
		if(bIncTitleBar)
		{
			// Add title bar size (whether it's visible or not)
			BorderSize.Top += NativeWindow->GetWindowTitleBarSize();
		}

		return BorderSize;
	}
#if PLATFORM_WINDOWS // || PLATFORM_LINUX
	return LayoutBorder;
#else
	return FMargin();
#endif
}


void SWindow::MoveWindowTo( FVector2D NewPosition )
{
	if (NativeWindow.IsValid())
	{
#if PLATFORM_LINUX
		// Slate code often expects cached screen position to be accurate immediately after the move.
		// This expectation is generally invalid (see UE-1308) as there may be a delay before the OS reports it back.
		// This hack sets the position speculatively, keeping Slate happy while also giving the OS chance to report it
		// correctly after or even during the actual call.
		FVector2D SpeculativeScreenPosition(FMath::TruncToInt(NewPosition.X), FMath::TruncToInt(NewPosition.Y));
		SetCachedScreenPosition(SpeculativeScreenPosition);
#endif // PLATFORM_LINUX

		NativeWindow->MoveWindowTo( FMath::TruncToInt(NewPosition.X), FMath::TruncToInt(NewPosition.Y) );
	}
	else
	{
		InitialDesiredScreenPosition = NewPosition;
	}
}

void SWindow::ReshapeWindow( FVector2D NewPosition, FVector2D NewSize )
{
	if (NativeWindow.IsValid())
	{
#if PLATFORM_LINUX
		// Slate code often expects cached screen position to be accurate immediately after the move.
		// This expectation is generally invalid (see UE-1308) as there may be a delay before the OS reports it back.
		// This hack sets the position speculatively, keeping Slate happy while also giving the OS chance to report it
		// correctly after or even during the actual call.
		FVector2D SpeculativeScreenPosition(FMath::TruncToInt(NewPosition.X), FMath::TruncToInt(NewPosition.Y));
		SetCachedScreenPosition(SpeculativeScreenPosition);
#endif // PLATFORM_LINUX

		NativeWindow->ReshapeWindow( FMath::TruncToInt(NewPosition.X), FMath::TruncToInt(NewPosition.Y), FMath::TruncToInt(NewSize.X), FMath::TruncToInt(NewSize.Y) );
	}
	else
	{
		InitialDesiredScreenPosition = NewPosition;
		InitialDesiredSize = NewSize;
	}

	SetCachedSize( NewSize );
}

void SWindow::ReshapeWindow( const FSlateRect& InNewShape )
{
	ReshapeWindow( FVector2D(InNewShape.Left, InNewShape.Top), FVector2D( InNewShape.Right - InNewShape.Left,  InNewShape.Bottom - InNewShape.Top) );
}

void SWindow::Resize( FVector2D NewSize )
{
	Morpher.Sequence.JumpToEnd();
	if ( Size != NewSize )
	{
		if (NativeWindow.IsValid())
		{
			NativeWindow->ReshapeWindow( FMath::TruncToInt(ScreenPosition.X), FMath::TruncToInt(ScreenPosition.Y), FMath::TruncToInt(NewSize.X), FMath::TruncToInt(NewSize.Y) );
		}
		else
		{
			InitialDesiredSize = NewSize;
		}
	}
	SetCachedSize(NewSize); 
}

FSlateRect SWindow::GetFullScreenInfo() const
{
	if (NativeWindow.IsValid())
	{
		int32 X;
		int32 Y;
		int32 Width;
		int32 Height;

		if ( NativeWindow->GetFullScreenInfo( X, Y, Width, Height ) )
		{
			return FSlateRect( X, Y, X + Width, Y + Height );
		}
	}

	return FSlateRect();
}

void SWindow::SetCachedScreenPosition(FVector2D NewPosition)
{
	ScreenPosition = NewPosition;
	OnWindowMoved.ExecuteIfBound( SharedThis( this ) );
}

void SWindow::SetCachedSize( FVector2D NewSize )
{
	if( NativeWindow.IsValid() )
	{
		NativeWindow->AdjustCachedSize( NewSize );
	}
	Size = NewSize;
}

bool SWindow::IsMorphing() const
{
	return Morpher.bIsActive && Morpher.Sequence.IsPlaying();
}

bool SWindow::IsMorphingSize() const
{
	return IsMorphing() && Morpher.bIsAnimatingWindowSize;
}


void SWindow::MorphToPosition( const FCurveSequence& Sequence, const float TargetOpacity, const FVector2D& TargetPosition )
{
	Morpher.bIsAnimatingWindowSize = false;
	Morpher.Sequence = Sequence;
	Morpher.TargetOpacity = TargetOpacity;
	UpdateMorphTargetPosition( TargetPosition );
	StartMorph();
}


void SWindow::MorphToShape( const FCurveSequence& Sequence, const float TargetOpacity, const FSlateRect& TargetShape )
{
	Morpher.bIsAnimatingWindowSize = true;
	Morpher.Sequence = Sequence;
	Morpher.TargetOpacity = TargetOpacity;
	UpdateMorphTargetShape(TargetShape);
	StartMorph();
}

void SWindow::StartMorph()
{
	Morpher.StartingOpacity = GetOpacity();
	Morpher.StartingMorphShape = FSlateRect( this->ScreenPosition.X, this->ScreenPosition.Y, this->ScreenPosition.X + this->Size.X, this->ScreenPosition.Y + this->Size.Y );
	Morpher.bIsPendingPlay = true;
	Morpher.bIsActive = true;
	Morpher.Sequence.JumpToStart();
}

const FSlateBrush* SWindow::GetWindowBackground() const
{
	return WindowBackground;
}

const FSlateBrush* SWindow::GetWindowOutline() const
{
	return &Style->OutlineBrush;
}

FSlateColor SWindow::GetWindowOutlineColor() const
{
	return Style->OutlineColor;
}

EVisibility SWindow::GetWindowVisibility() const
{
	return ( AcceptsInput() || FSlateApplicationBase::Get().IsWindowHousingInteractiveTooltip(SharedThis(this)) )
		? EVisibility::Visible
		: EVisibility::HitTestInvisible;
}

void SWindow::UpdateMorphTargetShape( const FSlateRect& TargetShape )
{
	Morpher.TargetMorphShape = TargetShape;
}

void SWindow::UpdateMorphTargetPosition( const FVector2D& TargetPosition )
{
	Morpher.TargetMorphShape.Left = Morpher.TargetMorphShape.Right = TargetPosition.X;
	Morpher.TargetMorphShape.Top = Morpher.TargetMorphShape.Bottom = TargetPosition.Y;
}

FVector2D SWindow::GetMorphTargetPosition() const
{
	return FVector2D( Morpher.TargetMorphShape.Left, Morpher.TargetMorphShape.Top );
}


FSlateRect SWindow::GetMorphTargetShape() const
{
	return Morpher.TargetMorphShape;
}

void SWindow::FlashWindow()
{
	if (TitleBar.IsValid())
	{
		TitleBar->Flash();
	}
}

void SWindow::BringToFront( bool bForce )
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->BringToFront( bForce );
	}
}

void SWindow::HACK_ForceToFront()
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->HACK_ForceToFront();
	}
}

TSharedPtr<FGenericWindow> SWindow::GetNativeWindow()
{
	return NativeWindow;
}

TSharedPtr<const FGenericWindow> SWindow::GetNativeWindow() const
{
	return NativeWindow;
} 

bool SWindow::IsDescendantOf( const TSharedPtr<SWindow>& ParentWindow ) const
{
	TSharedPtr<SWindow> CandidateToCheck = this->GetParentWindow();
	
	// Keep checking our parent until we get to the root of the tree or find the window we were looking for.
	while (CandidateToCheck.IsValid())
	{
		if (CandidateToCheck == ParentWindow)
		{
			// One of our ancestor windows is the ParentWindow we were looking for!
			return true;
		}

		// Consider the next ancestor
		CandidateToCheck = CandidateToCheck->GetParentWindow();
	}

	return false;
}

void SWindow::SetNativeWindow( TSharedRef<FGenericWindow> InNativeWindow )
{
	check( ! NativeWindow.IsValid() );
	NativeWindow = InNativeWindow;
}

void SWindow::SetContent( TSharedRef<SWidget> InContent )
{
	if ( bIsPopupWindow || bIsCursorDecoratorWindow )
	{
		this->ChildSlot.operator[]( InContent );
	}
	else
	{
		this->ContentSlot->operator[]( InContent );
	}
}

TSharedRef<const SWidget> SWindow::GetContent() const
{
	if ( bIsPopupWindow || bIsCursorDecoratorWindow )
	{
		return this->ChildSlot.GetChildAt(0);
	}
	else
	{
		return this->ContentSlot->GetWidget();
	}
}

bool SWindow::HasOverlay() const
{
	return WindowOverlay.IsValid();
}

SOverlay::FOverlaySlot& SWindow::AddOverlaySlot( const int32 ZOrder )
{
	if(!WindowOverlay.IsValid())
	{
		ensureMsg( false, TEXT("This window does not support overlays. The added slot will not be visible!") );
		WindowOverlay = SNew(SOverlay).Visibility( EVisibility::HitTestInvisible );
	}

	return WindowOverlay->AddSlot(ZOrder);
}

void SWindow::RemoveOverlaySlot( const TSharedRef<SWidget>& InContent )
{
	if(WindowOverlay.IsValid())
	{
		WindowOverlay->RemoveSlot( InContent );
	}
}

/** Return a new slot in the popup layer. Assumes that the window has a popup layer. */
FPopupLayerSlot& SWindow::AddPopupLayerSlot()
{
	ensure( PopupLayer.IsValid() );
	return PopupLayer->AddSlot();
}

/** Counterpart to AddPopupLayerSlot */
void SWindow::RemovePopupLayerSlot( const TSharedRef<SWidget>& WidgetToRemove )
{
	PopupLayer->RemoveSlot( WidgetToRemove );
}

/** @return should this window show up in the taskbar */
bool SWindow::AppearsInTaskbar() const
{
	return !bIsPopupWindow && !bIsToolTipWindow && !bIsCursorDecoratorWindow;
}

void SWindow::SetOnWindowActivated( const FOnWindowActivated& InDelegate )
{
	OnWindowActivated = InDelegate;
}

void SWindow::SetOnWindowDeactivated( const FOnWindowDeactivated& InDelegate )
{
	OnWindowDeactivated = InDelegate;
}

/** Sets the delegate to execute right before the window is closed */
void SWindow::SetOnWindowClosed( const FOnWindowClosed& InDelegate )
{
	OnWindowClosed = InDelegate;
}

/** Sets the delegate to execute right after the window has been moved */
void SWindow::SetOnWindowMoved( const FOnWindowMoved& InDelegate)
{
	OnWindowMoved = InDelegate;
}

/** Sets the delegate to override RequestDestroyWindow */
void SWindow::SetRequestDestroyWindowOverride( const FRequestDestroyWindowOverride& InDelegate )
{
	RequestDestroyWindowOverride = InDelegate;
}

/** Request that this window be destroyed. The window is not destroyed immediately. Instead it is placed in a queue for destruction on next Tick */
void SWindow::RequestDestroyWindow()
{
	if( RequestDestroyWindowOverride.IsBound() )
	{
		RequestDestroyWindowOverride.Execute( SharedThis(this) );
	}
	else
	{
		FSlateApplicationBase::Get().RequestDestroyWindow( SharedThis(this) );
	}
}

/** Warning: use Request Destroy Window whenever possible!  This method destroys the window immediately! */
void SWindow::DestroyWindowImmediately()
{
	check( NativeWindow.IsValid() );

	// Destroy the native window
	NativeWindow->Destroy();
}

/** Calls the OnWindowClosed delegate when this window is about to be closed */
void SWindow::NotifyWindowBeingDestroyed()
{
	OnWindowClosed.ExecuteIfBound( SharedThis( this ) );
}

/** Make the window visible */
void SWindow::ShowWindow()
{
	// Make sure the viewport is setup for this window
	if( !bHasEverBeenShown )
	{
		if( ensure( NativeWindow.IsValid() ) )
		{
			// We can only create a viewport after the window has been shown (otherwise the swap chain creation may fail)
			FSlateApplicationBase::Get().GetRenderer()->CreateViewport( SharedThis( this ) );
		}

		// Auto sized windows don't know their size until after their position is set.
		// Repositioning the window on show with the new size solves this.
		if ( SizingRule == ESizingRule::Autosized && AutoCenterRule != EAutoCenter::None )
		{
			SlatePrepass();
			ReshapeWindow( InitialDesiredScreenPosition - (GetDesiredSize() * 0.5f), GetDesiredSize() );
		}

		// Set the window to be maximized if we need to.  Note that this won't actually show the window if its not
		// already shown.
		InitialMaximize();
	}

	bHasEverBeenShown = true;

	if (NativeWindow.IsValid())
	{
		NativeWindow->Show();

		// If this is a tompost window (like a tooltip), make sure that its always rendered top most
		if( IsTopmostWindow() )
		{
			NativeWindow->BringToFront();
		}
	}
}

/** Make the window invisible */
void SWindow::HideWindow()
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->Hide();
	}
}

void SWindow::EnableWindow( bool bEnable )
{
	NativeWindow->Enable( bEnable );

	for( int32 ChildIndex = 0; ChildIndex < ChildWindows.Num(); ++ChildIndex )
	{
		ChildWindows[ChildIndex]->EnableWindow( bEnable );
	}
}


/** @return true if the window is visible, false otherwise*/
bool SWindow::IsVisible() const
{
	return NativeWindow.IsValid() && NativeWindow->IsVisible();
}

bool SWindow::IsWindowMaximized() const
{
	return NativeWindow->IsMaximized();
}

bool SWindow::IsWindowMinimized() const
{
	return NativeWindow->IsMinimized();
}


/** Maximize the window if bInitiallyMaximized is set */
void SWindow::InitialMaximize()
{
	if (NativeWindow.IsValid() && bInitiallyMaximized)
	{
		NativeWindow->Maximize();
	}
}

/**
 * Sets the opacity of this window
 *
 * @param	InOpacity	The new window opacity represented as a floating point scalar
 */
void SWindow::SetOpacity( const float InOpacity )
{
	if( Opacity != InOpacity )
	{
		check( NativeWindow.IsValid() );
		Opacity = InOpacity;
		NativeWindow->SetOpacity( Opacity );
	}
}


/** @return the window's current opacity */
float SWindow::GetOpacity() const
{
	return Opacity;
}

bool SWindow::SupportsTransparency() const
{
	return bIsTransparent;
}


/** @return A String representation of the widget */
FString SWindow::ToString() const
{
	return FString::Printf( *NSLOCTEXT("SWindow", "Window_Title", " Window : %s ").ToString(), *GetTitle().ToString() );
}

/** @return true if the window should be activated when first shown */
bool SWindow::ActivateWhenFirstShown() const
{
	return bActivateWhenFirstShown;
}

/** @return true if the window accepts input; false if the window is non-interactive */
bool SWindow::AcceptsInput() const
{
	return !bIsCursorDecoratorWindow && !bIsToolTipWindow;
}

/** @return true if the user decides the size of the window; false if the content determines the size of the window */
bool SWindow::IsUserSized() const
{
	return SizingRule == ESizingRule::UserSized;
}

bool SWindow::IsAutosized() const
{
	return SizingRule == ESizingRule::Autosized;
}

void SWindow::SetSizingRule( ESizingRule::Type InSizingRule )
{
	SizingRule = InSizingRule;
}

/** @return true if this is a vanilla window, or one being used for some special purpose: e.g. tooltip or menu */
bool SWindow::IsRegularWindow() const
{
	return !bIsPopupWindow && !bIsToolTipWindow && !bIsCursorDecoratorWindow;
}

/** @return true if the window should be on top of all other windows; false otherwise */
bool SWindow::IsTopmostWindow() const
{
	return bIsTopmostWindow;
}

/** @return true if mouse coordinates is within this window */
bool SWindow::IsScreenspaceMouseWithin(FVector2D ScreenspaceMouseCoordinate) const
{
	const FVector2D LocalMouseCoordinate = GetWindowGeometryInScreen().AbsoluteToLocal(ScreenspaceMouseCoordinate);
	return NativeWindow->IsPointInWindow(FMath::TruncToInt(LocalMouseCoordinate.X), FMath::TruncToInt(LocalMouseCoordinate.Y));
}

/** @return true if this is a user-sized window with a thick edge */
bool SWindow::HasSizingFrame() const
{
	return bHasSizingFrame;
}

/** @return true if this window has a maximize button/box on the titlebar area */
bool SWindow::HasMaximizeBox() const
{
	return bHasMaximizeButton;
}

/** @return true if this window has a minimize button/box on the titlebar area */
bool SWindow::HasMinimizeBox() const
{
	return bHasMinimizeButton;
}

FCursorReply SWindow::OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const
{
#if !PLATFORM_MAC // On Mac we depend on system's window resizing
	if (bHasSizingFrame)
	{
		if (WindowZone == EWindowZone::TopLeftBorder || WindowZone == EWindowZone::BottomRightBorder)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthEast);
		}
		else if (WindowZone == EWindowZone::BottomLeftBorder || WindowZone == EWindowZone::TopRightBorder)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeSouthWest);
		}
		else if (WindowZone == EWindowZone::TopBorder || WindowZone == EWindowZone::BottomBorder)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeUpDown);
		}
		else if (WindowZone == EWindowZone::LeftBorder || WindowZone == EWindowZone::RightBorder)
		{
			return FCursorReply::Cursor(EMouseCursor::ResizeLeftRight);
		}
	}
#endif
	return FCursorReply::Unhandled();
}

bool SWindow::OnIsActiveChanged( const FWindowActivateEvent& ActivateEvent )
{
	const bool bWasDeactivated = ( ActivateEvent.GetActivationType() == FWindowActivateEvent::EA_Deactivate );
	if ( bWasDeactivated )
	{
		OnWindowDeactivated.ExecuteIfBound();

		const EWindowMode::Type WindowMode = GetWindowMode();
		// If the window is not fullscreen, we do not want to automatically recapture the mouse unless an external UI such as Steam is open. Fullscreen windows we do.
		if( WindowMode != EWindowMode::Fullscreen && WidgetToFocusOnActivate.IsValid() && WidgetToFocusOnActivate.Pin()->HasMouseCapture() && !FSlateApplicationBase::Get().IsExternalUIOpened())
		{
			//For a windowed application with an OS border, if the user is giving focus back to the application by clicking on the close/(X) button, then we must clear 
			//the weak pointer to WidgetToFocus--so that the application's main viewport does not steal focus immediately (thus canceling the close attempt).
			
			//This change introduces a different bug where slate context is lost when closing popup menus.  However, this issue is negated by a 
			//change to FMenuStack::PushMenu, where we ReleaseMouseCapture when immediately shifting focus.
			WidgetToFocusOnActivate.Reset();
		}
	}
	else
	{
		OnWindowActivated.ExecuteIfBound();
	}

	return true;
}


void SWindow::Maximize()
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->Maximize();
	}
}

void SWindow::Restore()
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->Restore();
	}
}

void SWindow::Minimize()
{
	if (NativeWindow.IsValid())
	{
		NativeWindow->Minimize();
	}
}

int32 SWindow::GetCornerRadius()
{
	return IsRegularWindow() ? SWindowDefs::CornerRadius : 0;
}

bool SWindow::SupportsKeyboardFocus() const
{
	return !bIsToolTipWindow && !bIsCursorDecoratorWindow;
}

FReply SWindow::OnFocusReceived(const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent)
{
	if (InFocusEvent.GetCause() == EFocusCause::WindowActivate || FSlateApplicationBase::Get().IsExternalUIOpened())
	{
		TArray< TSharedRef<SWindow> > JustThisWindow;
		{
			JustThisWindow.Add( SharedThis(this) );
		}
		
		// If we're becoming active and we were set to restore keyboard focus to a specific widget
		// after reactivating, then do so now
		TSharedPtr< SWidget > PinnedWidgetToFocusOnActivate( WidgetToFocusOnActivate.Pin() );
		
		if(PinnedWidgetToFocusOnActivate.IsValid())
		{
			FWidgetPath WidgetToFocusPath;
			if( FSlateWindowHelper::FindPathToWidget( JustThisWindow, PinnedWidgetToFocusOnActivate.ToSharedRef(), WidgetToFocusPath ) )
			{
				FSlateApplicationBase::Get().SetKeyboardFocus( WidgetToFocusPath, EFocusCause::SetDirectly );
			}
		}
		else
		{
			FWidgetPath WindowWidgetPath;
			if( FSlateWindowHelper::FindPathToWidget( JustThisWindow, AsShared(), WindowWidgetPath ) )
			{
				FWeakWidgetPath WeakWindowPath(WindowWidgetPath);
				FSlateApplicationBase::Get().SetKeyboardFocus( WeakWindowPath.ToNextFocusedPath(EUINavigation::Next), EFocusCause::SetDirectly );
			}
		}
	}

	return FReply::Handled();
}

FReply SWindow::OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
#if PLATFORM_SPECIFIC_HACK && PLATFORM_LINUX
	if (bHasSizingFrame && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if ((WindowZone == EWindowZone::TopLeftBorder || WindowZone == EWindowZone::BottomRightBorder ||
			WindowZone == EWindowZone::BottomLeftBorder || WindowZone == EWindowZone::TopRightBorder ||
			WindowZone == EWindowZone::TopBorder || WindowZone == EWindowZone::BottomBorder ||
			WindowZone == EWindowZone::LeftBorder || WindowZone == EWindowZone::RightBorder ||
			WindowZone == EWindowZone::TitleBar)
			&& MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			MoveResizeZone = WindowZone;
			MoveResizeStart = MouseEvent.GetScreenSpacePosition();
			MoveResizeRect = GetRectInScreen();
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
	}
#endif
	if (bDragAnywhere && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		MoveResizeZone = WindowZone;
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SWindow::OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if (bDragAnywhere &&  this->HasMouseCapture() && MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		MoveResizeZone =  EWindowZone::Unspecified;
		return FReply::Handled().ReleaseMouseCapture();
	}
	else
	{
		return FReply::Unhandled();
	}
}

FReply SWindow::OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent )
{
	if ( bDragAnywhere && this->HasMouseCapture() && MouseEvent.IsMouseButtonDown(EKeys::LeftMouseButton) && MoveResizeZone != EWindowZone::TitleBar )
	{
		this->MoveWindowTo( ScreenPosition + MouseEvent.GetCursorDelta() );
		return FReply::Handled();
	}
	else
	{
		return FReply::Unhandled();
	}

}

FVector2D SWindow::ComputeDesiredSize() const
{
	const float Scale = FSlateApplicationBase::Get().GetApplicationScale();
	return SCompoundWidget::ComputeDesiredSize() * Scale;
}

const TArray< TSharedRef<SWindow> >& SWindow::GetChildWindows() const
{
	return ChildWindows;
}

TArray< TSharedRef<SWindow> >& SWindow::GetChildWindows()
{
	return ChildWindows;
}

void SWindow::AddChildWindow( const TSharedRef<SWindow>& ChildWindow )
{
	TSharedPtr<SWindow> PreviousParent = ChildWindow->ParentWindowPtr.Pin();
	if (PreviousParent.IsValid())
	{
		// This child already had a parent, so we are actually re-parenting it
		const bool bRemovedSuccessfully = PreviousParent->RemoveDescendantWindow(ChildWindow);
		check(bRemovedSuccessfully);
	}

	ChildWindow->ParentWindowPtr = SharedThis(this);
	ChildWindow->WindowBackground = &Style->ChildBackgroundBrush;

	FSlateApplicationBase::Get().ArrangeWindowToFrontVirtual( ChildWindows, ChildWindow );
}

TSharedPtr<SWindow> SWindow::GetParentWindow() const
{
	return ParentWindowPtr.Pin();
}


TSharedPtr<SWindow> SWindow::GetTopmostAncestor()
{
	TSharedPtr<SWindow> TopmostParentSoFar = SharedThis(this);
	while ( TopmostParentSoFar->ParentWindowPtr.IsValid() )
	{
		TopmostParentSoFar = TopmostParentSoFar->ParentWindowPtr.Pin();
	}

	return TopmostParentSoFar;
}

bool SWindow::RemoveDescendantWindow( const TSharedRef<SWindow>& DescendantToRemove )
{
	const bool bRemoved = 0 != ChildWindows.Remove(DescendantToRemove);

	for ( int32 ChildIndex=0; ChildIndex < ChildWindows.Num(); ++ChildIndex )
	{
		TSharedRef<SWindow>& ChildWindow = ChildWindows[ChildIndex];
		if ( ChildWindow->RemoveDescendantWindow( DescendantToRemove ))
		{
			// Reset to the non-child background style
			ChildWindow->WindowBackground = &Style->BackgroundBrush;
			return true;
		}
	}

	return false;
}

void SWindow::SetOnWorldSwitchHack( FOnSwitchWorldHack& InOnSwitchWorldHack )
{
	OnWorldSwitchHack = InOnSwitchWorldHack;
}

int32 SWindow::SwitchWorlds( int32 WorldId ) const
{
	return OnWorldSwitchHack.IsBound() ? OnWorldSwitchHack.Execute( WorldId ) : false;
}

bool PointWithinSlateRect(const FVector2D& Point, const FSlateRect& Rect)
{
	return Point.X >= Rect.Left && Point.X < Rect.Right &&
		Point.Y >= Rect.Top && Point.Y < Rect.Bottom;
}

EWindowZone::Type SWindow::GetCurrentWindowZone(FVector2D LocalMousePosition)
{
	// Don't allow position/resizing of window while in fullscreen mode by ignoring Title Bar/Border Zones
	if ( GetWindowMode() == EWindowMode::WindowedFullscreen || GetWindowMode() == EWindowMode::Fullscreen )
	{
		return EWindowZone::ClientArea;
	}
	else if(LocalMousePosition.X >= 0 && LocalMousePosition.X < Size.X &&
			LocalMousePosition.Y >= 0 && LocalMousePosition.Y < Size.Y)
	{
		int32 Row = 1;
		int32 Col = 1;
		if (SizingRule == ESizingRule::UserSized)
		{
			if (LocalMousePosition.X < (UserResizeBorder.Left + 5))
			{
				Col = 0;
			}
			else if (LocalMousePosition.X >= Size.X - (UserResizeBorder.Right + 5))
			{
				Col = 2;
			}

			if (LocalMousePosition.Y < (UserResizeBorder.Top + 5))
			{
				Row = 0;
			}
			else if (LocalMousePosition.Y >= Size.Y - (UserResizeBorder.Bottom + 5))
			{
				Row = 2;
			}

			// The actual border is smaller than the hit result zones
			// This grants larger corner areas to grab onto
			bool bInBorder =	LocalMousePosition.X < UserResizeBorder.Left ||
								LocalMousePosition.X >= Size.X - UserResizeBorder.Right ||
								LocalMousePosition.Y < UserResizeBorder.Top ||
								LocalMousePosition.Y >= Size.Y - UserResizeBorder.Bottom;

			if (!bInBorder)
			{
				Row = 1;
				Col = 1;
			}
		}

		static const EWindowZone::Type TypeZones[3][3] = 
		{
			{EWindowZone::TopLeftBorder,		EWindowZone::TopBorder,		EWindowZone::TopRightBorder},
			{EWindowZone::LeftBorder,			EWindowZone::ClientArea,	EWindowZone::RightBorder},
			{EWindowZone::BottomLeftBorder,		EWindowZone::BottomBorder,	EWindowZone::BottomRightBorder},
		};

		EWindowZone::Type InZone = TypeZones[Row][Col];
		if (InZone == EWindowZone::ClientArea)
		{
			// Hittest to see if the widget under the mouse should be treated as a title bar (i.e. should move the window)
			TArray< TSharedRef<SWindow> > ThisWindow;
			{
				ThisWindow.Add( SharedThis(this) );
			}

			FWidgetPath HitTestResults = FSlateApplicationBase::Get().LocateWindowUnderMouse(FSlateApplicationBase::Get().GetCursorPos(), ThisWindow);
			if( HitTestResults.Widgets.Num() > 0 )
			{
				const EWindowZone::Type ZoneOverride = HitTestResults.Widgets.Last().Widget->GetWindowZoneOverride();
				if( ZoneOverride != EWindowZone::Unspecified )
				{
					// The widget overrode the window zone
					InZone = ZoneOverride;
				}
				else if( HitTestResults.Widgets.Last().Widget == AsShared() )
				{
					// The window itself was hit, so check for a traditional title bar
					if ((LocalMousePosition.Y - UserResizeBorder.Top) < TitleBarSize)
					{
						InZone = EWindowZone::TitleBar;
					}
				}
			}
		}

		WindowZone = InZone;
	}
	else
	{
		WindowZone = EWindowZone::NotInWindow;
	}
	return WindowZone;
}

/**
 * Default constructor. Protected because SWindows must always be used via TSharedPtr. Instead, use FSlateApplication::MakeWindow()
 */
SWindow::SWindow()
	: bDragAnywhere( false )
	, Opacity( 1.0f )
	, SizingRule( ESizingRule::UserSized )
	, bIsTransparent( false )
	, bIsPopupWindow( false )
	, bIsToolTipWindow( false )
	, bIsTopmostWindow( false )
	, bSizeWillChangeOften( false )
	, bIsCursorDecoratorWindow( false )
	, bInitiallyMaximized( false )
	, bHasEverBeenShown( false )
	, bFocusWhenFirstShown(true)
	, bActivateWhenFirstShown(true)
	, bHasOSWindowBorder( false )
	, bHasMinimizeButton( false )
	, bHasMaximizeButton( false )
	, bHasSizingFrame( false )
	, bIsModalWindow( false )
	, InitialDesiredScreenPosition( FVector2D::ZeroVector )
	, InitialDesiredSize( FVector2D::ZeroVector )
	, ScreenPosition( FVector2D::ZeroVector )
	, PreFullscreenPosition( FVector2D::ZeroVector )
	, Size( FVector2D::ZeroVector )
	, ViewportSize( FVector2D::ZeroVector )
	, TitleBarSize( SWindowDefs::DefaultTitleBarSize )
	, ContentSlot(nullptr)
	, Style( &FCoreStyle::Get().GetWidgetStyle<FWindowStyle>("Window") )
	, WindowBackground( &Style->BackgroundBrush )
	, HittestGrid( MakeShareable(new FHittestGrid()) )
	, bShouldShowWindowContentDuringOverlay( false )
	, ExpectedMaxWidth( INDEX_NONE )
	, ExpectedMaxHeight( INDEX_NONE )
	, TitleBar()
{
}


int32 SWindow::PaintWindow( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const
{
	LayerId = Paint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled);
	LayerId = OutDrawElements.PaintDeferred( LayerId );
	return LayerId;
}


FOptionalSize SWindow::GetTitleBarSize() const
{
	return TitleBarSize;
}


void SWindow::SetFullWindowOverlayContent( TSharedPtr<SWidget> InContent )
{
	if( FullWindowOverlayWidget.IsValid() )
	{
		// Remove the last slot
		WindowOverlay->RemoveSlot( FullWindowOverlayWidget.ToSharedRef() );
		FullWindowOverlayWidget.Reset();
	}

	if( InContent.IsValid() )
	{
		FullWindowOverlayWidget = InContent;

		// Create a slot in our overlay to hold the content
		WindowOverlay->AddSlot( 1 )
		[
			InContent.ToSharedRef()
		];
	}
}

/** Toggle window between fullscreen and normal mode */
void SWindow::SetWindowMode( EWindowMode::Type NewWindowMode )
{
	EWindowMode::Type CurrentWindowMode = NativeWindow->GetWindowMode();

	if( CurrentWindowMode != NewWindowMode )
	{
		bool bFullscreen = NewWindowMode != EWindowMode::Windowed;

		bool bWasFullscreen = CurrentWindowMode != EWindowMode::Windowed;

		// We need to store off the screen position when entering fullscreen so that we can move the window back to its original position after leaving fullscreen
		if( bFullscreen )
		{
			PreFullscreenPosition = ScreenPosition;
		}

		NativeWindow->SetWindowMode( NewWindowMode );
	
		FVector2D vp = GetViewportSize();
		FSlateApplicationBase::Get().GetRenderer()->UpdateFullscreenState(SharedThis(this), vp.X, vp.Y);

		if( TitleArea.IsValid() )
		{
			// Collapse the Window title bar when switching to Fullscreen
			TitleArea->SetVisibility( (NewWindowMode == EWindowMode::Fullscreen || NewWindowMode == EWindowMode::WindowedFullscreen ) ? EVisibility::Collapsed : EVisibility::Visible );
		}

		if( bWasFullscreen )
		{
			// If we left fullscreen, reset the screen position;
			MoveWindowTo(PreFullscreenPosition);
		}
	}

}

bool SWindow::HasFullWindowOverlayContent() const
{
	return FullWindowOverlayWidget.IsValid();
}

void SWindow::BeginFullWindowOverlayTransition()
{
	bShouldShowWindowContentDuringOverlay = true; 
}

void SWindow::EndFullWindowOverlayTransition()
{
	bShouldShowWindowContentDuringOverlay = false;
}

EVisibility SWindow::GetWindowContentVisibility() const
{
	// The content of the window should be visible unless we have a full window overlay content
	// in which case the full window overlay content is visible but nothing under it
	return (bShouldShowWindowContentDuringOverlay == true || !FullWindowOverlayWidget.IsValid()) ? EVisibility::SelfHitTestInvisible : EVisibility::Hidden;
};
