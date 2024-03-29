// Copyright 1998-2015 Epic Games, Inc. All Rights Reserved.

#pragma once


/**
 * A Slate SpinBox resembles traditional spin boxes in that it is a widget that provides
 * keyboard-based and mouse-based manipulation of a numeric value.
 * Mouse-based manipulation: drag anywhere on the spinbox to change the value.
 * Keyboard-based manipulation: click on the spinbox to enter text mode.
 */
template<typename NumericType>
class SSpinBox
	: public SCompoundWidget
{
public:
	/** Notification for numeric value change */
	DECLARE_DELEGATE_OneParam( FOnValueChanged, NumericType );

	/** Notification for numeric value committed */
	DECLARE_DELEGATE_TwoParams( FOnValueCommitted, NumericType, ETextCommit::Type);

	SLATE_BEGIN_ARGS(SSpinBox<NumericType>)
		: _Style(&FCoreStyle::Get().GetWidgetStyle<FSpinBoxStyle>("SpinBox"))
		, _Value(0)
		, _MinValue(0)
		, _MaxValue(10)
		, _Delta(0)
		, _SliderExponent(1)
		, _Font( FCoreStyle::Get().GetFontStyle( TEXT( "NormalFont" ) ) )
		, _ContentPadding(  FMargin( 2.0f, 1.0f) )
		, _OnValueChanged()
		, _OnValueCommitted()
		, _ClearKeyboardFocusOnCommit( false )
		, _SelectAllTextOnCommit( true )
		, _MinDesiredWidth(0.0f)
		{}
	
		/** The style used to draw this spinbox */
		SLATE_STYLE_ARGUMENT( FSpinBoxStyle, Style )

		/** The value to display */
		SLATE_ATTRIBUTE( NumericType, Value )
		/** The minimum value that can be entered into the text edit box */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MinValue )
		/** The maximum value that can be entered into the text edit box */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MaxValue )
		/** The minimum value that can be specified by using the slider, defaults to MinValue */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MinSliderValue )
		/** The maximum value that can be specified by using the slider, defaults to MaxValue */
		SLATE_ATTRIBUTE( TOptional< NumericType >, MaxSliderValue )
		/** Delta to increment the value as the slider moves.  If not specified will determine automatically */
		SLATE_ATTRIBUTE( NumericType, Delta )
		/** Use exponential scale for the slider */
		SLATE_ATTRIBUTE( float, SliderExponent )
		/** Font used to display text in the slider */
		SLATE_ATTRIBUTE( FSlateFontInfo, Font )
		/** Padding to add around this widget and its internal widgets */
		SLATE_ARGUMENT( FMargin, ContentPadding )
		/** Called when the value is changed by slider or typing */
		SLATE_EVENT( FOnValueChanged, OnValueChanged )
		/** Called when the value is committed (by pressing enter) */
		SLATE_EVENT( FOnValueCommitted, OnValueCommitted )
		/** Called right before the slider begins to move */
		SLATE_EVENT( FSimpleDelegate, OnBeginSliderMovement )
		/** Called right after the slider handle is released by the user */
		SLATE_EVENT( FOnValueChanged, OnEndSliderMovement )
		/** Whether to clear keyboard focus when pressing enter to commit changes */
		SLATE_ATTRIBUTE( bool, ClearKeyboardFocusOnCommit )
		/** Whether to select all text when pressing enter to commit changes */
		SLATE_ATTRIBUTE( bool, SelectAllTextOnCommit )
		/** Minimum width that a spin box should be */
		SLATE_ATTRIBUTE( float, MinDesiredWidth )

	SLATE_END_ARGS()

	SSpinBox()
	{
	}
		
	/**
	 * Construct the widget
	 * 
	 * @param InArgs   A declaration from which to construct the widget
	 */
	void Construct( const FArguments& InArgs )
	{
		check(InArgs._Style);

		Style = InArgs._Style;

		ForegroundColor = InArgs._Style->ForegroundColor;

		ValueAttribute = InArgs._Value;
		OnValueChanged = InArgs._OnValueChanged;
		OnValueCommitted = InArgs._OnValueCommitted;
		OnBeginSliderMovement = InArgs._OnBeginSliderMovement;
		OnEndSliderMovement = InArgs._OnEndSliderMovement;
		MinDesiredWidth = InArgs._MinDesiredWidth;
	
		MinValue = InArgs._MinValue;
		MaxValue = InArgs._MaxValue;
		MinSliderValue = (InArgs._MinSliderValue.Get().IsSet()) ? InArgs._MinSliderValue : MinValue;
		MaxSliderValue = (InArgs._MaxSliderValue.Get().IsSet()) ? InArgs._MaxSliderValue : MaxValue;

		UpdateIsSpinRangeUnlimited();
	
		SliderExponent = InArgs._SliderExponent;

		DistanceDragged = 0.0f;
		CachedExternalValue = 0;
		InternalValue = 0;
		PreDragValue = 0;

		Delta = InArgs._Delta.Get();
	
		BackgroundHoveredBrush = &InArgs._Style->HoveredBackgroundBrush;
		BackgroundBrush = &InArgs._Style->BackgroundBrush;
		ActiveFillBrush = &InArgs._Style->ActiveFillBrush;
		InactiveFillBrush = &InArgs._Style->InactiveFillBrush;
		const FMargin& TextMargin = InArgs._Style->TextPadding;

		bDragging = false;
		CachedExternalValue = ValueAttribute.Get();
		InternalValue = ValueAttribute.Get();

		this->ChildSlot
		.Padding( InArgs._ContentPadding )
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding( TextMargin )
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center)
			[
				SAssignNew(TextBlock, STextBlock)
				.Font(InArgs._Font)
				.Text( this, &SSpinBox<NumericType>::GetValueAsString )
				.MinDesiredWidth( this, &SSpinBox<NumericType>::GetTextMinDesiredWidth )
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding( TextMargin )
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center) 
			[
				SAssignNew(EditableText, SEditableText)
				.Visibility( EVisibility::Collapsed )
				.Font(InArgs._Font)
				.SelectAllTextWhenFocused( true )
				.Text( this, &SSpinBox<NumericType>::GetValueAsText )
				.OnIsTypedCharValid_Static(TCharacterFilter<NumericType>::IsValid)
				.OnTextCommitted( this, &SSpinBox<NumericType>::TextField_OnTextCommitted )
				.ClearKeyboardFocusOnCommit( InArgs._ClearKeyboardFocusOnCommit )
				.SelectAllTextOnCommit( InArgs._SelectAllTextOnCommit )
				.MinDesiredWidth( this, &SSpinBox<NumericType>::GetTextMinDesiredWidth )
				.VirtualKeyboardType(EKeyboardType::Keyboard_Number)
			]			

			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Fill) 
			.VAlign(VAlign_Center) 
			[
				SNew(SImage)
				.Image( &InArgs._Style->ArrowsImage )
				.ColorAndOpacity( FSlateColor::UseForeground())
			]
		];
	}
	
	virtual int32 OnPaint( const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyClippingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled ) const override
	{
		const bool bActiveFeedback = IsHovered() || bDragging;

		const FSlateBrush* BackgroundImage = bActiveFeedback ?
			BackgroundHoveredBrush :
			BackgroundBrush;

		const FSlateBrush* FillImage = bActiveFeedback ?
			ActiveFillBrush :
			InactiveFillBrush;

		const int32 BackgroundLayer = LayerId;

		const bool bEnabled = ShouldBeEnabled( bParentEnabled );
		const ESlateDrawEffect::Type DrawEffects = bEnabled ? ESlateDrawEffect::None : ESlateDrawEffect::DisabledEffect;

		FSlateDrawElement::MakeBox(
			OutDrawElements,
			BackgroundLayer,
			AllottedGeometry.ToPaintGeometry(),
			BackgroundImage,
			MyClippingRect,
			DrawEffects,
			BackgroundImage->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
			);

		const int32 FilledLayer = BackgroundLayer + 1;

		//if there is a spin range limit, draw the filler bar
		if (!bUnlimitedSpinRange)
		{
			double Value = ValueAttribute.Get();
			if( Delta != 0.0f )
			{
				Value= Snap(Value, Delta); // snap floating point value to nearest Delta
			}

			float FractionFilled = Fraction(Value, GetMinSliderValue(), GetMaxSliderValue());
			const float CachedSliderExponent = SliderExponent.Get();
			if (CachedSliderExponent != 1)
			{
				FractionFilled = 1.0f - FMath::Pow( 1.0f - FractionFilled, CachedSliderExponent);
			}
			const FVector2D FillSize( AllottedGeometry.Size.X * FractionFilled, AllottedGeometry.Size.Y );

			if ( ! IsInTextMode() )
			{
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					FilledLayer,
					AllottedGeometry.ToPaintGeometry(FVector2D(0,0), FillSize),
					FillImage,
					MyClippingRect,
					DrawEffects,
					FillImage->GetTint(InWidgetStyle) * InWidgetStyle.GetColorAndOpacityTint()
					);
			}
		}

		return FMath::Max( FilledLayer, SCompoundWidget::OnPaint(Args, AllottedGeometry, MyClippingRect, OutDrawElements, FilledLayer, InWidgetStyle, bEnabled ) );
	}

	/**
	 * The system calls this method to notify the widget that a mouse button was pressed within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonDown( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton )
		{
			DistanceDragged = 0;
			PreDragValue = InternalValue;
			CachedMousePosition = MouseEvent.GetScreenSpacePosition().IntPoint();
			return FReply::Handled().CaptureMouse(SharedThis(this)).UseHighPrecisionMouseMovement(SharedThis(this)).SetUserFocus(SharedThis(this), EFocusCause::Mouse);
		}
		else
		{

			return FReply::Unhandled();
		}
	}
	
	/**
	 * The system calls this method to notify the widget that a mouse button was release within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseButtonUp( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && this->HasMouseCapture() )
		{
			if( bDragging )
			{
				NotifyValueCommitted();
			}

			bDragging = false;
			if ( DistanceDragged < FSlateApplication::Get().GetDragTriggerDistnace() )
			{
				EnterTextMode();
				return FReply::Handled().ReleaseMouseCapture().SetUserFocus(EditableText.ToSharedRef(), EFocusCause::SetDirectly).SetMousePos(CachedMousePosition);
			}

			return FReply::Handled().ReleaseMouseCapture().SetMousePos(CachedMousePosition);
			
		}
		else
		{
			return FReply::Unhandled();
		}
	}
	
	/**
	 * The system calls this method to notify the widget that a mouse moved within it. This event is bubbled.
	 *
	 * @param MyGeometry The Geometry of the widget receiving the event
	 * @param MouseEvent Information about the input event
	 * @return Whether the event was handled along with possible requests for the system to take action.
	 */
	virtual FReply OnMouseMove( const FGeometry& MyGeometry, const FPointerEvent& MouseEvent ) override
	{
		if ( this->HasMouseCapture() )
		{
			if (!bDragging)
			{
				DistanceDragged += FMath::Abs(MouseEvent.GetCursorDelta().X);
				if ( DistanceDragged > FSlateApplication::Get().GetDragTriggerDistnace() )
				{
					ExitTextMode();
					bDragging = true;
				}

				if( bDragging )
				{
					OnBeginSliderMovement.ExecuteIfBound();
				}

				// Cache the mouse, even if not dragging cache it.
				CachedMousePosition = MouseEvent.GetScreenSpacePosition().IntPoint();
			}
			else
			{
				double NewValue = 0;

				// Increments the spin based on delta mouse movement.

				//if we have a range to draw in
				if ( !bUnlimitedSpinRange) 
				{
					const float CachedSliderExponent = SliderExponent.Get();
					// The amount currently filled in the spinbox, needs to be calculated to do deltas correctly.
					float FractionFilled = Fraction(InternalValue, GetMinSliderValue(), GetMaxSliderValue());
						
					if (CachedSliderExponent != 1)
					{
						FractionFilled = 1.0f - FMath::Pow( 1.0f - FractionFilled, CachedSliderExponent);
					}
					FractionFilled *= MyGeometry.GetDrawSize().X;

					// Now add the delta to the fraction filled, this causes the spin.
					FractionFilled += MouseEvent.GetCursorDelta().X;
						
					// Clamp the fraction to be within the bounds of the geometry.
					FractionFilled = FMath::Clamp(FractionFilled, 0.0f, MyGeometry.GetDrawSize().X);

					// Convert the fraction filled to a percent.
					float Percent = FMath::Clamp(FractionFilled / MyGeometry.GetDrawSize().X, 0.0f, 1.0f);
					if (CachedSliderExponent != 1)
					{
						// Have to convert the percent to the proper value due to the exponent component to the spin.
						Percent = 1.0f - FMath::Pow(1.0f - Percent, 1.0 / CachedSliderExponent);
					}

					NewValue = FMath::LerpStable<double>(GetMinSliderValue(), GetMaxSliderValue(), Percent);
				}
				else
				{
					//we have no range specified, so increment scaled by our current value		
					const double CurrentValue = FMath::Clamp<double>(FMath::Abs(InternalValue),1.0,TNumericLimits<NumericType>::Max());
					const float MouseDelta = FMath::Abs(MouseEvent.GetCursorDelta().X / MyGeometry.GetDrawSize().X);
					const float Sign = (MouseEvent.GetCursorDelta().X > 0) ? 1.f : -1.f;
					NewValue = InternalValue + (Sign * MouseDelta * FMath::Pow(CurrentValue, SliderExponent.Get()));
				}
			
				CommitValue( NewValue, CommittedViaSpin, ETextCommit::OnEnter );
			}

			return FReply::Handled();
		}

		return FReply::Unhandled();
	}

	virtual FCursorReply OnCursorQuery( const FGeometry& MyGeometry, const FPointerEvent& CursorEvent ) const override
	{
		return bDragging ? 
			FCursorReply::Cursor( EMouseCursor::None ) :
			FCursorReply::Cursor( EMouseCursor::ResizeLeftRight );
	}

	virtual bool SupportsKeyboardFocus() const
	{
		// SSpinBox is focusable.
		return true;
	}


	virtual FReply OnFocusReceived( const FGeometry& MyGeometry, const FFocusEvent& InFocusEvent ) override
	{
		if ( !bDragging && (InFocusEvent.GetCause() == EFocusCause::Navigation || InFocusEvent.GetCause() == EFocusCause::SetDirectly) )
		{
			EnterTextMode();
			return FReply::Handled().SetUserFocus(EditableText.ToSharedRef(), InFocusEvent.GetCause());
		}
		else
		{
			return FReply::Unhandled();
		}
	}
	
	virtual FReply OnKeyDown( const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent ) override
	{
		const FKey Key = InKeyEvent.GetKey();
		if ( Key == EKeys::Escape && HasMouseCapture())
		{
			bDragging = false;

			InternalValue = PreDragValue;
			NotifyValueCommitted();
			return FReply::Handled().ReleaseMouseCapture().SetMousePos(CachedMousePosition);
		}
		else if ( Key == EKeys::Up || Key == EKeys::Right )
		{
			CommitValue( InternalValue + Delta, CommittedViaSpin, ETextCommit::OnEnter );
			ExitTextMode();
			return FReply::Handled();	
		}
		else if ( Key == EKeys::Down || Key == EKeys::Left )
		{
			CommitValue( InternalValue - Delta, CommittedViaSpin, ETextCommit::OnEnter );
			ExitTextMode();
			return FReply::Handled();
		}
		else if ( Key == EKeys::Enter )
		{
			EnterTextMode();
			return FReply::Handled().SetUserFocus(EditableText.ToSharedRef(), EFocusCause::Navigation);
		}
		else
		{
			return FReply::Unhandled();
		}
	}
	
	virtual bool HasKeyboardFocus() const
	{
		// The spinbox is considered focused when we are typing it text.
		return SCompoundWidget::HasKeyboardFocus() || (EditableText.IsValid() && EditableText->HasKeyboardFocus());
	}

	/** See the Value attribute */
	float GetValue() const { return ValueAttribute.Get(); }
	void SetValue(const TAttribute<NumericType>& InValueAttribute) 
	{
		ValueAttribute = InValueAttribute; 
		CommitValue(InValueAttribute.Get(), ECommitMethod::CommittedViaSpin, ETextCommit::Default);
	}

	/** See the MinValue attribute */
	NumericType GetMinValue() const { return MinValue.Get().Get(TNumericLimits<NumericType>::Lowest()); }
	void SetMinValue(const TAttribute<TOptional<NumericType>>& InMinValue) 
	{ 
		MinValue = InMinValue;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MaxValue attribute */
	NumericType GetMaxValue() const { return MaxValue.Get().Get(TNumericLimits<NumericType>::Max()); }
	void SetMaxValue(const TAttribute<TOptional<NumericType>>& InMaxValue) 
	{ 
		MaxValue = InMaxValue; 
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MinSliderValue attribute */
	NumericType GetMinSliderValue() const { return MinSliderValue.Get().Get(TNumericLimits<NumericType>::Lowest()); }
	void SetMinSliderValue(const TAttribute<TOptional<NumericType>>& InMinSliderValue) 
	{ 
		MinSliderValue = (InMinSliderValue.Get().IsSet()) ? InMinSliderValue : MinValue;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the MaxSliderValue attribute */
	NumericType GetMaxSliderValue() const { return MaxSliderValue.Get().Get(TNumericLimits<NumericType>::Max()); }
	void SetMaxSliderValue(const TAttribute<TOptional<NumericType>>& InMaxSliderValue) 
	{ 
		MaxSliderValue = (InMaxSliderValue.Get().IsSet()) ? InMaxSliderValue : MaxValue;;
		UpdateIsSpinRangeUnlimited();
	}

	/** See the Delta attribute */
	NumericType GetDelta() const { return Delta; }
	void SetDelta(NumericType InDelta) { Delta = InDelta; }

	/** See the SliderExponent attribute */
	float GetSliderExponent() const { return SliderExponent.Get(); }
	void SetSliderExponent(const TAttribute<float>& InSliderExponent) { SliderExponent = InSliderExponent; }
	
	/** See the MinDesiredWidth attribute */
	float GetMinDesiredWidth() const { return SliderExponent.Get(); }
	void SetMinDesiredWidth(const TAttribute<float>& InMinDesiredWidth) { MinDesiredWidth = InMinDesiredWidth; }

protected:
	/** Make the spinbox switch to keyboard-based input mode. */
	void EnterTextMode()
	{
		TextBlock->SetVisibility( EVisibility::Collapsed );
		EditableText->SetVisibility( EVisibility::Visible );
	}
	
	/** Make the spinbox switch to mouse-based input mode. */
	void ExitTextMode()
	{
		TextBlock->SetVisibility( EVisibility::Visible );
		EditableText->SetVisibility( EVisibility::Collapsed );
	}

	/** @return the value being observed by the spinbox as a string */
	FString GetValueAsString() const
	{
		auto CurrentValue = ValueAttribute.Get();
		if( Delta != 0 )
		{
			CurrentValue = (NumericType)Snap(CurrentValue, Delta);
		}
		
		return TTypeToString<NumericType>::ToSanitizedString(CurrentValue);
	}

	/** @return the value being observed by the spinbox as FText - todo: spinbox FText support (reimplement me) */
	FText GetValueAsText() const
	{
		return FText::FromString(GetValueAsString());
	}
	
	/**
	 * Invoked when the text field commits its text.
	 *
	 * @param NewText		The value of text coming from the editable text field.
	 * @param CommitInfo	Information about the source of the commit
	 */
	void TextField_OnTextCommitted( const FText& NewText, ETextCommit::Type CommitInfo )
	{
		if (CommitInfo != ETextCommit::OnEnter)
		{
			ExitTextMode();
		}

		NumericType NewValue = 0;
		bool bEvalResult = false;
		FString NewString = NewText.ToString();
		
		if (NewString.IsNumeric())
		{
			TTypeFromString<NumericType>::FromString( NewValue, *NewString );
			bEvalResult = true;
		}
		else
		{
			float FloatValue = 0.f;
			bEvalResult = FMath::Eval( *NewString, FloatValue  );
			NewValue = NumericType(FloatValue);
		}
				
		if (bEvalResult)
		{
			CommitValue( NewValue, CommittedViaTypeIn, CommitInfo );
		}
	}


	/** How user changed the value in the spinbox */
	enum ECommitMethod
	{
		CommittedViaSpin,
		CommittedViaTypeIn	
	};

	/**
	 * Call this method when the user's interaction has changed the value
	 *
	 * @param NewValue               Value resulting from the user's interaction
	 * @param CommitMethod           Did the user type in the value or spin to it.
	 * @param OriginalCommitInfo	 If the user typed in the value, information about the source of the commit
	 */
	void CommitValue( double NewValue, ECommitMethod CommitMethod, ETextCommit::Type OriginalCommitInfo )
	{
		if( CommitMethod == CommittedViaSpin )
		{
			NewValue = FMath::Clamp<double>( NewValue, GetMinSliderValue(), GetMaxSliderValue() );
		}

		NewValue = FMath::Clamp<double>( NewValue, GetMinValue(), GetMaxValue() );

		if ( !ValueAttribute.IsBound() )
		{
			if (TIsIntegralType<NumericType>::Value)
			{
				// Round floating point to avoid 'jumping' when spinning down integral values 
				ValueAttribute.Set( (NumericType)FMath::FloorToDouble(NewValue+0.5) );
			}
			else
			{
				ValueAttribute.Set( (NumericType)NewValue );
			}
		}

		auto CurrentValue = ValueAttribute.Get();

		// If not in spin mode, there is no need to jump to the value from the external source, continue to use the committed value.
		if(CommitMethod == CommittedViaSpin)
		{
			// This will detect if an external force has changed the value. Internally it will abandon the delta calculated this tick and update the internal value instead.
			if(CurrentValue != CachedExternalValue)
			{
				NewValue = CurrentValue;
			}
		}

		// Update the internal value, this needs to be done before rounding.
		InternalValue = NewValue;

		// If needed, round this value to the delta. Internally the value is not held to the Delta but externally it appears to be.
		if ( CommitMethod == CommittedViaSpin )
		{
			if( Delta != 0 )
			{
				NewValue = Snap(NewValue, Delta); // snap numeric point value to nearest Delta
			}
		}

		if( CommitMethod == CommittedViaTypeIn )
		{
			OnValueCommitted.ExecuteIfBound( NumericType(NewValue), OriginalCommitInfo );
		}

		OnValueChanged.ExecuteIfBound( NumericType(NewValue) );

		// Update the cache of the external value to what the user believes the value is now.
		CachedExternalValue = ValueAttribute.Get();

		// This ensures that dragging is cleared if focus has been removed from this widget in one of the delegate calls, such as when spawning a modal dialog.
		if ( !this->HasMouseCapture() )
		{
			bDragging = false;
		}
	}
	
	void NotifyValueCommitted() const
	{
		const auto CurrentValue = (NumericType)InternalValue; // internal value should be snapped and clamped at this point
		OnValueCommitted.ExecuteIfBound( CurrentValue, ETextCommit::OnEnter );
		OnEndSliderMovement.ExecuteIfBound( CurrentValue );
	}

	/** @return true when we are in keyboard-based input mode; false otherwise */
	bool IsInTextMode() const
	{
		return ( EditableText->GetVisibility() == EVisibility::Visible );
	}

	/** Calculates range fraction. Possible to use on full numeric range  */
	static float Fraction(double InValue, NumericType InMinValue, NumericType InMaxValue)
	{
		const double HalfMax = InMaxValue*0.5;
		const double HalfMin = InMinValue*0.5;
		const double HalfVal = InValue*0.5;

		return (float)FMath::Clamp((HalfVal - HalfMin)/(HalfMax - HalfMin), 0.0, 1.0);
	}

	/** Snaps value to a nearest grid multiple. Clamps value if it's outside numeric range  */
	static double Snap(double InValue, double InGrid)
	{
		double Result = FMath::GridSnap(InValue, InGrid);
		return FMath::Clamp<double>(Result, TNumericLimits<NumericType>::Lowest(), TNumericLimits<NumericType>::Max());
	}
	
	/**
	 * Helper structure to check if a given character is supported by underlying numeric type
	 */
	template<typename T, typename U = void>
	struct TCharacterFilter
	{
		static bool IsValid(const TCHAR InChar)
		{
			static FString ValidChars(TEXT("1234567890-.")); // for integers and doubles
			int32 OutUnused;
			return ValidChars.FindChar( InChar, OutUnused );
		}
	};

	template<typename U> // nested partial specialization is allowed
	struct TCharacterFilter<float, U>
	{
		static bool IsValid(const TCHAR InChar)
		{
			static FString ValidChars(TEXT("1234567890()-+=\\/.,*^%%"));
			int32 OutUnused;
			return ValidChars.FindChar( InChar, OutUnused );		
		}
	};

private:
	TAttribute<NumericType> ValueAttribute;
	FOnValueChanged OnValueChanged;
	FOnValueCommitted OnValueCommitted;
	FSimpleDelegate OnBeginSliderMovement;
	FOnValueChanged OnEndSliderMovement;
	TSharedPtr<STextBlock> TextBlock;
	TSharedPtr<SEditableText> EditableText;

	/** True when no range is specified, spinner can be spun indefinitely */
	bool bUnlimitedSpinRange;
	void UpdateIsSpinRangeUnlimited()
	{
		bUnlimitedSpinRange = !((MinValue.Get().IsSet() && MaxValue.Get().IsSet()) || (MinSliderValue.Get().IsSet() && MaxSliderValue.Get().IsSet()));
	}

	const FSpinBoxStyle* Style;

	const FSlateBrush* BackgroundHoveredBrush;
	const FSlateBrush* BackgroundBrush;
	const FSlateBrush* ActiveFillBrush;
	const FSlateBrush* InactiveFillBrush;

	float DistanceDragged;
	NumericType Delta;
	TAttribute<float> SliderExponent;
	TAttribute< TOptional<NumericType> > MinValue;
	TAttribute< TOptional<NumericType> > MaxValue;
	TAttribute< TOptional<NumericType> > MinSliderValue;
	TAttribute< TOptional<NumericType> > MaxSliderValue;

	/** Prevents the spinbox from being smaller than desired in certain cases (e.g. when it is empty) */
	TAttribute<float> MinDesiredWidth;
	float GetTextMinDesiredWidth() const
	{
		return FMath::Max(0.0f, MinDesiredWidth.Get() - Style->ArrowsImage.ImageSize.X);
	}

	/** Whether the user is dragging the slider */
	bool bDragging;
	
	/** Cached mouse position to restore after scrolling. */
	FIntPoint CachedMousePosition;

	/** This value represents what the spinbox believes the value to be, regardless of delta and the user binding to an int. 
		The spinbox will always count using floats between values, this is important to keep it flowing smoothly and feeling right, 
		and most importantly not conflicting with the user truncating the value to an int. */
	double InternalValue;

	/** The state of InternalValue before a drag operation was started */
	double PreDragValue;

	/** This is the cached value the user believes it to be (usually different due to truncation to an int). Used for identifying 
		external forces on the spinbox and syncing the internal value to them. Synced when a value is committed to the spinbox. */
	NumericType CachedExternalValue;
};
