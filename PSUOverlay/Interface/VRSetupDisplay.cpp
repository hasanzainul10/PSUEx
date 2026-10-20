#include "VRSetupDisplay.h"

#include "../../Misc/Log.h"
#include "../../PSUExConfig.h"
#include "../../PSUVR/VRBindings.h"
#include "../../PSUVR/VRControllerDiagram.h"
#include "../../PSUVR/VRHands.h"
#include "../../PSUVR/VRHud.h"
#include "../../PSUVR/VRSystem.h"
#include "../../PSUVR/VRUiPointer.h"

namespace
{
	bool s_openRequest = false;

	constexpr float kEyeHeightMin = 1.2f;
	constexpr float kEyeHeightMax = 2.0f;

	const char *StepTitle( int step )
	{
		static const char *const titles[] = { "Welcome", "Play position", "Eye height", "Hands", "Controls" };
		return titles[ step ];
	}

	void Bullet( const char *text )
	{
		ImGui::Bullet();
		ImGui::TextWrapped( "%s", text );
	}
}

VRSetupDisplay::VRSetupDisplay() : IFWinCtrl( "VRSetupDisplay", false )
{
}

void VRSetupDisplay::RequestOpen()
{
	s_openRequest = true;
}

void VRSetupDisplay::Open()
{
	const auto &vr = Config::Get().vr;
	m_motion = vr.m_inputMode == "motion";
	m_step = Step::Welcome;
	m_recentered = false;
	m_measured = false;
	m_eyeHeightOnEntry = vr.m_fpEyeHeight;
	m_calibRequestOnEntry = vr.m_handCalibRequest;
	m_bEnabled = true;
	PSU_LOG( "VRSetup: opened (%s input)\n", m_motion ? "motion" : "gamepad" );
}

void VRSetupDisplay::Close( bool finished )
{
	auto &vr = Config::Get().vr;
	vr.m_setupDone = true;
	Config::Get().SaveToFile();
	m_bEnabled = false;
	PSUVR::VRBindings::ShowToast( finished ? "VR setup saved" : "VR setup skipped" );
	PSU_LOG( "VRSetup: %s, eye height %.2f m, hands %s\n", finished ? "finished" : "skipped", vr.m_fpEyeHeight,
		vr.m_handCalibrated ? "calibrated" : "automatic" );
}

bool VRSetupDisplay::StepShown( Step step ) const
{
	return m_motion || ( step != Step::Hands && step != Step::Controls );
}

void VRSetupDisplay::Go( int direction )
{
	int step = static_cast< int >( m_step );
	do
	{
		step += direction;
	} while( step > 0 && step < static_cast< int >( Step::Count ) && !StepShown( static_cast< Step >( step ) ) );
	if( step < 0 || step >= static_cast< int >( Step::Count ) )
	{
		return;
	}
	m_step = static_cast< Step >( step );
	if( m_step == Step::EyeHeight )
	{
		m_eyeHeightOnEntry = Config::Get().vr.m_fpEyeHeight;
		m_measured = false;
	}
}

void VRSetupDisplay::DrawWelcome()
{
	ImGui::TextWrapped( "This sets up VR in a few steps: where you play, your eye height%s.",
		m_motion ? ", the hand angle and the controls" : "" );
	ImGui::Spacing();
	if( m_motion )
	{
		ImGui::TextWrapped( "Point at this panel with the right controller:" );
		Bullet( "Trigger: click a button or a checkbox. Hold it and drag to move a slider." );
		Bullet( "Right stick up / down: scroll." );
		Bullet( "Right grip held while pointing: move the panel nearer or farther (also the right stick while the grip is held)." );
		Bullet( "The game doesn't get the controllers while this panel is open." );
	}
	else
	{
		ImGui::TextWrapped( "Use the mouse on the desktop window to click through the steps." );
	}
	ImGui::Spacing();
	ImGui::TextWrapped( "Skip closes the wizard; run it again any time from VR Settings > General > Run VR setup again." );
}

void VRSetupDisplay::DrawPosition()
{
	ImGui::TextWrapped( "Stand or sit where you'll play and look straight ahead, then press Recenter." );
	ImGui::TextWrapped( "Forward and the centre of your play space are taken from where your head is now." );
	ImGui::Spacing();
	if( ImGui::Button( "Recenter", ImVec2( 200.0f, 0.0f ) ) )
	{
		PSUVR::System::Get().RequestRecenter();
		m_recentered = true;
		PSU_LOG( "VRSetup: recenter requested\n" );
	}
	if( m_recentered )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "Recentered" );
	}
	ImGui::Spacing();
	ImGui::TextDisabled( "Later: both grips + right Cross (A on other controllers), or Ctrl+F9." );
}

void VRSetupDisplay::DrawEyeHeight()
{
	auto &vr = Config::Get().vr;
	float head = 0.0f;
	const bool haveFloor = PSUVR::System::Get().HeadHeightAboveFloor( head );
	if( haveFloor )
	{
		ImGui::TextWrapped( "Stand straight and look ahead, then press Measure: your head height above the floor "
			"becomes the first person eye height." );
		ImGui::Text( "Your head now: %.2f m above the floor", head );
		ImGui::Spacing();
		if( ImGui::Button( "Measure", ImVec2( 200.0f, 0.0f ) ) )
		{
			const float clamped = head < kEyeHeightMin ? kEyeHeightMin : ( head > kEyeHeightMax ? kEyeHeightMax : head );
			vr.m_fpEyeHeight = clamped;
			m_measured = true;
			PSU_LOG( "VRSetup: measured head %.2f m, eye height %.2f m\n", head, clamped );
		}
		if( m_measured )
		{
			ImGui::SameLine();
			ImGui::Text( "Eye height set to %.2f m", vr.m_fpEyeHeight );
		}
	}
	else
	{
		ImGui::TextWrapped( "Your headset doesn't report the floor, so set the eye height by hand: how high the "
			"view sits above your character's feet in first person." );
	}
	ImGui::Spacing();
	ImGui::SliderFloat( "Eye height (m)", &vr.m_fpEyeHeight, kEyeHeightMin, kEyeHeightMax, "%.2f" );
	if( ImGui::Button( "Keep current" ) )
	{
		vr.m_fpEyeHeight = m_eyeHeightOnEntry;
		m_measured = false;
		Go( 1 );
	}
	ImGui::SameLine();
	ImGui::TextDisabled( "(%.2f m, back to the value from before this step)", m_eyeHeightOnEntry );
}

void VRSetupDisplay::DrawHands()
{
	auto &vr = Config::Get().vr;
	ImGui::TextWrapped( "In first person your character's hands follow the controllers. The hand angle comes with a "
		"PS VR2 Sense calibration; for other controllers (or if the hands look bent), calibrate it:" );
	Bullet( "Press Calibrate, then hold both arms straight out in front." );
	Bullet( "Hands in fists around the controllers, palms facing each other, knuckles forward." );
	Bullet( "Keep still: a buzz each second, a long buzz after 5 seconds when the angle is taken. It is saved." );
	Bullet( "Later: Calibrate in VR Settings, or both grips + both triggers." );
	ImGui::Spacing();
	if( !vr.m_handsFollow || vr.m_cameraMode != "firstPerson" )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.8f, 0.3f, 1.0f ), "Needs first person with \"hands follow the controllers\" on; "
			"the capture waits until then." );
	}
	if( ImGui::Button( "Calibrate", ImVec2( 200.0f, 0.0f ) ) )
	{
		++vr.m_handCalibRequest;
		PSU_LOG( "VRSetup: hand calibration requested\n" );
	}
	ImGui::SameLine();
	if( ImGui::Button( "Keep the PS VR2 defaults" ) )
	{
		const VRSettings defaults;
		vr.m_handCalibrated = defaults.m_handCalibrated;
		vr.m_handRotL0 = defaults.m_handRotL0;
		vr.m_handRotL1 = defaults.m_handRotL1;
		vr.m_handRotL2 = defaults.m_handRotL2;
		vr.m_handRotL3 = defaults.m_handRotL3;
		vr.m_handRotR0 = defaults.m_handRotR0;
		vr.m_handRotR1 = defaults.m_handRotR1;
		vr.m_handRotR2 = defaults.m_handRotR2;
		vr.m_handRotR3 = defaults.m_handRotR3;
		PSU_LOG( "VRSetup: hand calibration back to the PS VR2 defaults\n" );
		Go( 1 );
		return;
	}

	if( vr.m_handCalibRequest == m_calibRequestOnEntry )
	{
		return;
	}
	const PSUVR::VRHands::CalibStatus status = PSUVR::VRHands::CalibrationStatus();
	switch( status.phase )
	{
		case PSUVR::VRHands::CalibStatus::Counting:
			ImGui::Text( "Hold still... %.0f", status.secondsLeft + 0.5f );
			ImGui::ProgressBar( 1.0f - status.secondsLeft / 5.0f, ImVec2( -1.0f, 0.0f ) );
			break;
		case PSUVR::VRHands::CalibStatus::Done:
			ImGui::TextColored( ImVec4( 0.4f, 1.0f, 0.4f, 1.0f ), "Calibrated (%d hand%s), saved.", status.hands,
				status.hands == 1 ? "" : "s" );
			break;
		case PSUVR::VRHands::CalibStatus::Missed:
			ImGui::TextColored( ImVec4( 1.0f, 0.5f, 0.4f, 1.0f ), "No controller was tracked at the capture; press Calibrate again." );
			break;
		default:
			ImGui::TextDisabled( "Waiting for your character in first person..." );
			break;
	}
}

void VRSetupDisplay::DrawControls()
{
	const auto &vr = Config::Get().vr;
	Bullet( "Look down at a controller to see its label card (what each button does right now)." );
	Bullet( "Hold a grip still for a moment to see that grip's labels." );
	Bullet( "Both grips + right stick click: label cards on / off." );
	Bullet( "Both grips + left Square: VR settings." );
	Bullet( "Left Triangle: the game's chat log (R2)." );
	ImGui::Spacing();
	PSUVR::DrawControllerDiagram( vr.m_motionProfile );
}

void VRSetupDisplay::Render()
{
	const auto &vr = Config::Get().vr;
	const bool inHeadset = PSUVR::VRHud::Get().Active();
	if( s_openRequest )
	{
		s_openRequest = false;
		Open();
	}
	else if( !m_autoChecked && inHeadset )
	{
		// First stereo frame: open once for a new user with the motion controllers. In gamepad
		// mode there is no pointer, so only the settings button opens it.
		m_autoChecked = true;
		if( vr.m_enabled && !vr.m_setupDone && vr.m_inputMode == "motion" )
		{
			Open();
		}
	}
	PSUVR::VRUiPointer::SetWizardOpen( m_bEnabled );
	if( !m_bEnabled )
	{
		return;
	}
	// VRDisplay renders first and clears the cursor when its own window is closed.
	if( inHeadset || PSUVR::VRUiPointer::Wanted() )
	{
		ImGui::GetIO().MouseDrawCursor = true;
	}

	const ImVec2 display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos( ImVec2( display.x * 0.5f, display.y * 0.5f ), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
	ImGui::SetNextWindowSize( ImVec2( 640.0f, 560.0f ), ImGuiCond_FirstUseEver );
	if( ImGui::Begin( "VR Setup", nullptr, ImGuiWindowFlags_NoCollapse ) )
	{
		// Step list along the top.
		int number = 0, total = 0, current = 0;
		for( int i = 0; i < static_cast< int >( Step::Count ); ++i )
		{
			if( StepShown( static_cast< Step >( i ) ) )
			{
				++total;
				if( i == static_cast< int >( m_step ) )
				{
					current = total;
				}
			}
		}
		for( int i = 0; i < static_cast< int >( Step::Count ); ++i )
		{
			if( !StepShown( static_cast< Step >( i ) ) )
			{
				continue;
			}
			++number;
			if( number > 1 )
			{
				ImGui::SameLine();
				ImGui::TextDisabled( ">" );
				ImGui::SameLine();
			}
			if( i == static_cast< int >( m_step ) )
			{
				ImGui::Text( "%d. %s", number, StepTitle( i ) );
			}
			else
			{
				ImGui::TextDisabled( "%d. %s", number, StepTitle( i ) );
			}
		}
		ImGui::Separator();

		const float footer = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y * 2.0f;
		ImGui::BeginChild( "vr_setup_step", ImVec2( 0.0f, -footer ) );
		switch( m_step )
		{
			case Step::Welcome: DrawWelcome(); break;
			case Step::Position: DrawPosition(); break;
			case Step::EyeHeight: DrawEyeHeight(); break;
			case Step::Hands: DrawHands(); break;
			case Step::Controls: DrawControls(); break;
			default: break;
		}
		ImGui::EndChild();

		ImGui::Separator();
		const ImVec2 buttonSize( 140.0f, 0.0f );
		if( ImGui::Button( "Skip", buttonSize ) )
		{
			Close( false );
		}
		ImGui::SameLine();
		ImGui::TextDisabled( "Step %d of %d", current, total );
		ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - buttonSize.x * 2.0f - ImGui::GetStyle().ItemSpacing.x );
		ImGui::BeginDisabled( m_step == Step::Welcome );
		if( ImGui::Button( "Back", buttonSize ) )
		{
			Go( -1 );
		}
		ImGui::EndDisabled();
		ImGui::SameLine();
		if( current == total )
		{
			if( ImGui::Button( "Finish", buttonSize ) )
			{
				Close( true );
			}
		}
		else if( ImGui::Button( "Next", buttonSize ) )
		{
			Go( 1 );
		}
	}
	ImGui::End();
}
