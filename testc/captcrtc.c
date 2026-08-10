/* LICENSE>>
Copyright 2020 Soji Yamakawa (CaptainYS, http://www.ysflight.com)

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

<< LICENSE */
#include "egb.h"
#include "io.h"
#include "vmif.h"

static char EGB_work[EgbWorkSize];

void SetScreenMode(int m1,int m2);

int main(void)
{
	EGB_init(EGB_work,EgbWorkSize);

	SetScreenMode(1,1);

	SetScreenMode(2,2);

	SetScreenMode(3,3);
	SetScreenMode(3,5);
	SetScreenMode(3,10);

	SetScreenMode(4,4);
	SetScreenMode(4,6);

	SetScreenMode(5,5);
	SetScreenMode(5,3);
	SetScreenMode(5,10);

	SetScreenMode(6,4);
	SetScreenMode(6,6);

	SetScreenMode(7,7);
	SetScreenMode(7,9);

	SetScreenMode(8,8);
	SetScreenMode(8,11);

	SetScreenMode(9,7);
	SetScreenMode(9,9);

	SetScreenMode(10,3);
	SetScreenMode(10,5);
	SetScreenMode(10,10);

	SetScreenMode(11,8);
	SetScreenMode(11,11);

	SetScreenMode(12,-1);
	SetScreenMode(13,-1);
	SetScreenMode(14,-1);
	SetScreenMode(15,-1);
	SetScreenMode(16,-1);
	SetScreenMode(17,-1);
	SetScreenMode(18,-1);

	return 0;
}

void SetScreenMode(int m1,int m2)
{
	EGB_resolution(EGB_work,0,m1);
	if(0<=m2)
	{
		EGB_resolution(EGB_work,1,m2);
	}

	IOWriteByte(TOWNSIO_VM_HOST_IF_DATA,m1);
	IOWriteByte(TOWNSIO_VM_HOST_IF_DATA,m2);
	IOWriteByte(TOWNSIO_VM_HOST_IF_CMD_STATUS,TOWNS_VMIF_CMD_CAPTURE_CRTC);
}
