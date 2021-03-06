/***********************************************************************************************************************
*                                                                                                                      *
* ANTIKERNEL v0.1                                                                                                      *
*                                                                                                                      *
* Copyright (c) 2012-2020 Andrew D. Zonenberg                                                                          *
* All rights reserved.                                                                                                 *
*                                                                                                                      *
* Redistribution and use in source and binary forms, with or without modification, are permitted provided that the     *
* following conditions are met:                                                                                        *
*                                                                                                                      *
*    * Redistributions of source code must retain the above copyright notice, this list of conditions, and the         *
*      following disclaimer.                                                                                           *
*                                                                                                                      *
*    * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the       *
*      following disclaimer in the documentation and/or other materials provided with the distribution.                *
*                                                                                                                      *
*    * Neither the name of the author nor the names of any contributors may be used to endorse or promote products     *
*      derived from this software without specific prior written permission.                                           *
*                                                                                                                      *
* THIS SOFTWARE IS PROVIDED BY THE AUTHORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED   *
* TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL *
* THE AUTHORS BE HELD LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES        *
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR       *
* BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT *
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE       *
* POSSIBILITY OF SUCH DAMAGE.                                                                                          *
*                                                                                                                      *
***********************************************************************************************************************/

#include "../scopehal/scopehal.h"
#include "ParallelBusDecoder.h"

using namespace std;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Construction / destruction

ParallelBusDecoder::ParallelBusDecoder(string color)
	: ProtocolDecoder(OscilloscopeChannel::CHANNEL_TYPE_DIGITAL, color, CAT_BUS)
{
	//Set up channels
	char tmp[32];
	for(size_t i=0; i<16; i++)
	{
		snprintf(tmp, sizeof(tmp), "din%zu", i);
		m_signalNames.push_back(tmp);
		m_channels.push_back(NULL);
	}

	m_widthname = "Width";
	m_parameters[m_widthname] = ProtocolDecoderParameter(ProtocolDecoderParameter::TYPE_FLOAT);
	m_parameters[m_widthname].SetIntVal(0);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Factory methods

bool ParallelBusDecoder::ValidateChannel(size_t i, OscilloscopeChannel* channel)
{
	if( (i < 16) && (channel->GetType() == OscilloscopeChannel::CHANNEL_TYPE_DIGITAL) )
		return true;
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Accessors

string ParallelBusDecoder::GetProtocolName()
{
	return "Parallel Bus";
}

void ParallelBusDecoder::SetDefaultName()
{
	char hwname[256];
	snprintf(hwname, sizeof(hwname), "ParallelBus(%s)", m_channels[0]->m_displayname.c_str());
	m_hwname = hwname;
	m_displayname = m_hwname;
}

bool ParallelBusDecoder::NeedsConfig()
{
	return true;
}

bool ParallelBusDecoder::IsOverlay()
{
	//Probably doesn't make sense to be an overlay since we're not tied to the single bit we started decoding on
	return false;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Serialization

void ParallelBusDecoder::LoadParameters(const YAML::Node& node, IDTable& table)
{
	ProtocolDecoder::LoadParameters(node, table);
	m_width = m_parameters[m_widthname].GetIntVal();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Actual decoder logic

void ParallelBusDecoder::Refresh()
{
	//Figure out how wide our input is
	m_width = m_parameters[m_widthname].GetIntVal();

	//Make sure we have an input for each channel in use
	vector<DigitalWaveform*> inputs;
	for(int i=0; i<m_width; i++)
	{
		if(m_channels[i] == NULL)
		{
			LogDebug("err 1\n");
			SetData(NULL);
			return;
		}
		auto din = dynamic_cast<DigitalWaveform*>(m_channels[i]->GetData());
		if(din == NULL)
		{
			LogDebug("err 2\n");
			SetData(NULL);
			return;
		}
		inputs.push_back(din);
	}
	if(inputs.empty())
	{
		SetData(NULL);
		return;
	}

	//Figure out length of the output
	size_t len = inputs[0]->m_samples.size();
	for(int j=1; j<m_width; j++)
		len = min(len, inputs[j]->m_samples.size());

	//Merge all of our samples
	//TODO: handle variable sample rates etc
	auto cap = new DigitalBusWaveform;
	cap->Resize(len);
	cap->CopyTimestamps(inputs[0]);
	#pragma omp parallel for
	for(size_t i=0; i<len; i++)
	{
		for(int j=0; j<m_width; j++)
			cap->m_samples[i].push_back(inputs[j]->m_samples[i]);
	}
	SetData(cap);

	//Copy our time scales from the input
	cap->m_timescale = inputs[0]->m_timescale;
	cap->m_startTimestamp = inputs[0]->m_startTimestamp;
	cap->m_startPicoseconds = inputs[0]->m_startPicoseconds;

	//Set all unused channels to NULL
	for(size_t i=m_width; i < 16; i++)
	{
		if(m_channels[i] != NULL)
		{
			if(m_channels[i] != NULL)
				m_channels[i]->Release();
			m_channels[i] = NULL;
		}
	}
}
