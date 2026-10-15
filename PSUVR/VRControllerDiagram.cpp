#include "VRControllerDiagram.h"

#include "VRMotionLayout.h"

#include <imgui.h>

#include <cstdio>

namespace PSUVR
{
	namespace
	{
		using VRMotionLayout::Control;
		using VRMotionLayout::Output;

		const VRMotionLayout::Entry *Find( int hand, Control control )
		{
			for( const VRMotionLayout::Entry &entry : VRMotionLayout::kLayout )
			{
				if( entry.hand == hand && entry.control == control )
				{
					return &entry;
				}
			}
			return nullptr;
		}

		// Where each control sits on a left controller, relative to its centre (the right
		// controller is mirrored). Face buttons are on the inner side.
		ImVec2 ControlOffset( Control control )
		{
			switch( control )
			{
				case Control::Trigger: return ImVec2( 0.0f, -78.0f );
				case Control::Stick: return ImVec2( -6.0f, -30.0f );
				case Control::StickClick: return ImVec2( -6.0f, -30.0f );
				case Control::Secondary: return ImVec2( 22.0f, -6.0f );
				case Control::Primary: return ImVec2( 12.0f, 18.0f );
				case Control::Grip: return ImVec2( -24.0f, 48.0f );
				default: return ImVec2( 0.0f, 0.0f );
			}
		}

		// Label rows, top to bottom.
		const Control kRows[] = { Control::Trigger, Control::Stick, Control::StickClick, Control::Secondary,
			Control::Primary, Control::Grip };

		void DrawController( ImDrawList *draw, const ImVec2 &center, bool psvr2 )
		{
			const ImU32 body = ImGui::GetColorU32( ImGuiCol_FrameBgActive );
			const ImU32 edge = ImGui::GetColorU32( ImGuiCol_Border );
			// Handle.
			draw->AddRectFilled( ImVec2( center.x - 26.0f, center.y - 10.0f ), ImVec2( center.x + 26.0f, center.y + 78.0f ), body, 22.0f );
			// Head: the Sense controllers' ring, or a Touch-style ring.
			const float ring = psvr2 ? 46.0f : 40.0f;
			draw->AddCircleFilled( ImVec2( center.x, center.y - 30.0f ), ring, body, 32 );
			draw->AddCircle( ImVec2( center.x, center.y - 30.0f ), ring + 6.0f, edge, 32, psvr2 ? 5.0f : 3.0f );
			// Trigger lip.
			draw->AddRectFilled( ImVec2( center.x - 14.0f, center.y - 86.0f ), ImVec2( center.x + 14.0f, center.y - 72.0f ), edge, 4.0f );
		}
	}

	void DrawControllerDiagram( const std::string &profile )
	{
		const bool psvr2 = VRMotionLayout::IsPsvr2( profile );
		ImDrawList *draw = ImGui::GetWindowDrawList();
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const float width = ImGui::GetContentRegionAvail().x < 600.0f ? 600.0f : ImGui::GetContentRegionAvail().x;
		const float height = 250.0f;
		const ImU32 text = ImGui::GetColorU32( ImGuiCol_Text );
		const ImU32 line = ImGui::GetColorU32( ImGuiCol_TextDisabled );
		const ImU32 dot = ImGui::GetColorU32( ImGuiCol_CheckMark );

		const float labelWidth = width * 0.30f;
		const ImVec2 centers[ 2 ] = {
			ImVec2( origin.x + width * 0.40f, origin.y + 120.0f ),
			ImVec2( origin.x + width * 0.60f, origin.y + 120.0f ) };
		static const char *const kTitles[ 2 ] = { "Left", "Right" };

		for( int hand = 0; hand < 2; ++hand )
		{
			DrawController( draw, centers[ hand ], psvr2 );
			const ImVec2 titleSize = ImGui::CalcTextSize( kTitles[ hand ] );
			draw->AddText( ImVec2( centers[ hand ].x - titleSize.x * 0.5f, origin.y + height - titleSize.y ), line, kTitles[ hand ] );

			const float mirror = hand == 0 ? 1.0f : -1.0f;
			int row = 0;
			for( Control control : kRows )
			{
				const VRMotionLayout::Entry *entry = Find( hand, control );
				if( !entry )
				{
					continue;
				}
				char label[ 96 ];
				sprintf_s( label, "%s: %s", VRMotionLayout::ControlName( profile, hand, control ),
					VRMotionLayout::OutputName( entry->plain ) );
				const ImVec2 size = ImGui::CalcTextSize( label );
				const float y = origin.y + 14.0f + row * 36.0f;
				++row;

				const ImVec2 offset = ControlOffset( control );
				const ImVec2 point( centers[ hand ].x + offset.x * mirror, centers[ hand ].y + offset.y );
				// Left labels end at the left column's edge, right labels start at the right one.
				const float labelX = hand == 0 ? origin.x + labelWidth - size.x : origin.x + width - labelWidth;
				draw->AddText( ImVec2( labelX, y - size.y * 0.5f ), text, label );
				const ImVec2 anchor( hand == 0 ? origin.x + labelWidth + 4.0f : origin.x + width - labelWidth - 4.0f, y );
				draw->AddLine( anchor, point, line, 1.0f );
				draw->AddCircleFilled( point, 4.0f, dot, 12 );
			}
		}
		ImGui::Dummy( ImVec2( width, height ) );

		// Both-grip chords and the special combinations.
		if( ImGui::BeginTable( "##chords", 2, ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp ) )
		{
			ImGui::TableSetupColumn( "Hold both grips +" );
			ImGui::TableSetupColumn( "Does" );
			ImGui::TableHeadersRow();
			for( const VRMotionLayout::Entry &entry : VRMotionLayout::kLayout )
			{
				if( entry.chord == Output::None )
				{
					continue;
				}
				ImGui::TableNextRow();
				ImGui::TableNextColumn();
				ImGui::Text( "%s %s", entry.hand == 0 ? "Left" : "Right", VRMotionLayout::ControlName( profile, entry.hand, entry.control ) );
				ImGui::TableNextColumn();
				ImGui::TextUnformatted( VRMotionLayout::OutputName( entry.chord ) );
			}
			ImGui::EndTable();
		}
		ImGui::TextWrapped( "%s", VRMotionLayout::kDpadToggleText );
		ImGui::TextDisabled( "%s", VRMotionLayout::ReservedText( profile ) );
	}
}
