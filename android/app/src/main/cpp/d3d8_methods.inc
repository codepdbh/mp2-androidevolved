// Generado por scripts/pc_recomp/generate_d3d8_bridge.py desde d3d8.h.
if(kind==uint32_t(Kind::Device)){auto resource=static_cast<IDirect3DDevice8*>(object);switch(method){
case 4:{/*GetAvailableTextureMem*/*argumentCount=1;return resource->GetAvailableTextureMem();}
case 5:{/*ResourceManagerDiscardBytes*/*argumentCount=2;return resource->ResourceManagerDiscardBytes(static_cast<DWORD>(args[1]));}
case 6:{/*GetDirect3D*/*argumentCount=2;IDirect3D8* object1=nullptr;HRESULT status=resource->GetDirect3D(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::D3D8):0);return status;}
case 7:{/*GetDeviceCaps*/*argumentCount=2;return resource->GetDeviceCaps(static_cast<D3DCAPS8*>(pointer(args[1])));}
case 8:{/*GetDisplayMode*/*argumentCount=2;return resource->GetDisplayMode(static_cast<D3DDISPLAYMODE*>(pointer(args[1])));}
case 9:{/*GetCreationParameters*/*argumentCount=2;return resource->GetCreationParameters(static_cast<D3DDEVICE_CREATION_PARAMETERS*>(pointer(args[1])));}
case 10:{/*SetCursorProperties*/*argumentCount=4;return resource->SetCursorProperties(static_cast<UINT>(args[1]),static_cast<UINT>(args[2]),nativeObject<IDirect3DSurface8>(args[3]));}
case 11:{/*SetCursorPosition*/*argumentCount=4;resource->SetCursorPosition(static_cast<UINT>(args[1]),static_cast<UINT>(args[2]),static_cast<DWORD>(args[3]));return 0;}
case 12:{/*ShowCursor*/*argumentCount=2;return resource->ShowCursor(static_cast<WINBOOL>(args[1]));}
case 16:{/*GetBackBuffer*/*argumentCount=4;IDirect3DSurface8* object3=nullptr;HRESULT status=resource->GetBackBuffer(static_cast<UINT>(args[1]),static_cast<D3DBACKBUFFER_TYPE>(args[2]),args[3]?&object3:nullptr);if(args[3])write(args[3],SUCCEEDED(status)?wrap(object3,Kind::Surface):0);return status;}
case 17:{/*GetRasterStatus*/*argumentCount=2;return resource->GetRasterStatus(static_cast<D3DRASTER_STATUS*>(pointer(args[1])));}
case 18:{/*SetGammaRamp*/*argumentCount=3;resource->SetGammaRamp(static_cast<DWORD>(args[1]),static_cast<const D3DGAMMARAMP*>(pointer(args[2])));return 0;}
case 19:{/*GetGammaRamp*/*argumentCount=2;resource->GetGammaRamp(static_cast<D3DGAMMARAMP*>(pointer(args[1])));return 0;}
case 28:{/*CopyRects*/*argumentCount=6;return resource->CopyRects(nativeObject<IDirect3DSurface8>(args[1]),static_cast<const RECT*>(pointer(args[2])),static_cast<UINT>(args[3]),nativeObject<IDirect3DSurface8>(args[4]),static_cast<const POINT*>(pointer(args[5])));}
case 29:{/*UpdateTexture*/*argumentCount=3;return resource->UpdateTexture(nativeObject<IDirect3DBaseTexture8>(args[1]),nativeObject<IDirect3DBaseTexture8>(args[2]));}
case 30:{/*GetFrontBuffer*/*argumentCount=2;return resource->GetFrontBuffer(nativeObject<IDirect3DSurface8>(args[1]));}
case 31:{/*SetRenderTarget*/*argumentCount=3;return resource->SetRenderTarget(nativeObject<IDirect3DSurface8>(args[1]),nativeObject<IDirect3DSurface8>(args[2]));}
case 32:{/*GetRenderTarget*/*argumentCount=2;IDirect3DSurface8* object1=nullptr;HRESULT status=resource->GetRenderTarget(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Surface):0);return status;}
case 33:{/*GetDepthStencilSurface*/*argumentCount=2;IDirect3DSurface8* object1=nullptr;HRESULT status=resource->GetDepthStencilSurface(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Surface):0);return status;}
case 34:{/*BeginScene*/*argumentCount=1;return resource->BeginScene();}
case 35:{/*EndScene*/*argumentCount=1;return resource->EndScene();}
case 36:{/*Clear*/*argumentCount=7;return resource->Clear(static_cast<DWORD>(args[1]),static_cast<const D3DRECT*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<D3DCOLOR>(args[4]),floatValue(args[5]),static_cast<DWORD>(args[6]));}
case 37:{/*SetTransform*/*argumentCount=3;return resource->SetTransform(static_cast<D3DTRANSFORMSTATETYPE>(args[1]),static_cast<const D3DMATRIX*>(pointer(args[2])));}
case 38:{/*GetTransform*/*argumentCount=3;return resource->GetTransform(static_cast<D3DTRANSFORMSTATETYPE>(args[1]),static_cast<D3DMATRIX*>(pointer(args[2])));}
case 39:{/*MultiplyTransform*/*argumentCount=3;return resource->MultiplyTransform(static_cast<D3DTRANSFORMSTATETYPE>(args[1]),static_cast<const D3DMATRIX*>(pointer(args[2])));}
case 40:{/*SetViewport*/*argumentCount=2;return resource->SetViewport(static_cast<const D3DVIEWPORT8*>(pointer(args[1])));}
case 41:{/*GetViewport*/*argumentCount=2;return resource->GetViewport(static_cast<D3DVIEWPORT8*>(pointer(args[1])));}
case 42:{/*SetMaterial*/*argumentCount=2;return resource->SetMaterial(static_cast<const D3DMATERIAL8*>(pointer(args[1])));}
case 43:{/*GetMaterial*/*argumentCount=2;return resource->GetMaterial(static_cast<D3DMATERIAL8*>(pointer(args[1])));}
case 44:{/*SetLight*/*argumentCount=3;return resource->SetLight(static_cast<DWORD>(args[1]),static_cast<const D3DLIGHT8*>(pointer(args[2])));}
case 45:{/*GetLight*/*argumentCount=3;return resource->GetLight(static_cast<DWORD>(args[1]),static_cast<D3DLIGHT8*>(pointer(args[2])));}
case 46:{/*LightEnable*/*argumentCount=3;return resource->LightEnable(static_cast<DWORD>(args[1]),static_cast<WINBOOL>(args[2]));}
case 47:{/*GetLightEnable*/*argumentCount=3;return resource->GetLightEnable(static_cast<DWORD>(args[1]),static_cast<WINBOOL*>(pointer(args[2])));}
case 48:{/*SetClipPlane*/*argumentCount=3;return resource->SetClipPlane(static_cast<DWORD>(args[1]),static_cast<const float*>(pointer(args[2])));}
case 49:{/*GetClipPlane*/*argumentCount=3;return resource->GetClipPlane(static_cast<DWORD>(args[1]),static_cast<float*>(pointer(args[2])));}
case 50:{/*SetRenderState*/*argumentCount=3;return resource->SetRenderState(static_cast<D3DRENDERSTATETYPE>(args[1]),static_cast<DWORD>(args[2]));}
case 51:{/*GetRenderState*/*argumentCount=3;return resource->GetRenderState(static_cast<D3DRENDERSTATETYPE>(args[1]),static_cast<DWORD*>(pointer(args[2])));}
case 52:{/*BeginStateBlock*/*argumentCount=1;return resource->BeginStateBlock();}
case 53:{/*EndStateBlock*/*argumentCount=2;return resource->EndStateBlock(static_cast<DWORD*>(pointer(args[1])));}
case 54:{/*ApplyStateBlock*/*argumentCount=2;return resource->ApplyStateBlock(static_cast<DWORD>(args[1]));}
case 55:{/*CaptureStateBlock*/*argumentCount=2;return resource->CaptureStateBlock(static_cast<DWORD>(args[1]));}
case 56:{/*DeleteStateBlock*/*argumentCount=2;return resource->DeleteStateBlock(static_cast<DWORD>(args[1]));}
case 57:{/*CreateStateBlock*/*argumentCount=3;return resource->CreateStateBlock(static_cast<D3DSTATEBLOCKTYPE>(args[1]),static_cast<DWORD*>(pointer(args[2])));}
case 58:{/*SetClipStatus*/*argumentCount=2;return resource->SetClipStatus(static_cast<const D3DCLIPSTATUS8*>(pointer(args[1])));}
case 59:{/*GetClipStatus*/*argumentCount=2;return resource->GetClipStatus(static_cast<D3DCLIPSTATUS8*>(pointer(args[1])));}
case 60:{/*GetTexture*/*argumentCount=3;IDirect3DBaseTexture8* object2=nullptr;HRESULT status=resource->GetTexture(static_cast<DWORD>(args[1]),args[2]?&object2:nullptr);if(args[2])write(args[2],SUCCEEDED(status)?wrapTexture(object2):0);return status;}
case 61:{/*SetTexture*/*argumentCount=3;return resource->SetTexture(static_cast<DWORD>(args[1]),nativeObject<IDirect3DBaseTexture8>(args[2]));}
case 62:{/*GetTextureStageState*/*argumentCount=4;return resource->GetTextureStageState(static_cast<DWORD>(args[1]),static_cast<D3DTEXTURESTAGESTATETYPE>(args[2]),static_cast<DWORD*>(pointer(args[3])));}
case 63:{/*SetTextureStageState*/*argumentCount=4;return resource->SetTextureStageState(static_cast<DWORD>(args[1]),static_cast<D3DTEXTURESTAGESTATETYPE>(args[2]),static_cast<DWORD>(args[3]));}
case 64:{/*ValidateDevice*/*argumentCount=2;return resource->ValidateDevice(static_cast<DWORD*>(pointer(args[1])));}
case 65:{/*GetInfo*/*argumentCount=4;return resource->GetInfo(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD>(args[3]));}
case 66:{/*SetPaletteEntries*/*argumentCount=3;return resource->SetPaletteEntries(static_cast<UINT>(args[1]),static_cast<const PALETTEENTRY*>(pointer(args[2])));}
case 67:{/*GetPaletteEntries*/*argumentCount=3;return resource->GetPaletteEntries(static_cast<UINT>(args[1]),static_cast<PALETTEENTRY*>(pointer(args[2])));}
case 68:{/*SetCurrentTexturePalette*/*argumentCount=2;return resource->SetCurrentTexturePalette(static_cast<UINT>(args[1]));}
case 69:{/*GetCurrentTexturePalette*/*argumentCount=2;return resource->GetCurrentTexturePalette(static_cast<UINT*>(pointer(args[1])));}
case 70:{/*DrawPrimitive*/*argumentCount=4;return resource->DrawPrimitive(static_cast<D3DPRIMITIVETYPE>(args[1]),static_cast<UINT>(args[2]),static_cast<UINT>(args[3]));}
case 71:{/*DrawIndexedPrimitive*/*argumentCount=6;return resource->DrawIndexedPrimitive(static_cast<D3DPRIMITIVETYPE>(args[1]),static_cast<UINT>(args[2]),static_cast<UINT>(args[3]),static_cast<UINT>(args[4]),static_cast<UINT>(args[5]));}
case 72:{/*DrawPrimitiveUP*/*argumentCount=5;return resource->DrawPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(args[1]),static_cast<UINT>(args[2]),static_cast<const void*>(pointer(args[3])),static_cast<UINT>(args[4]));}
case 73:{/*DrawIndexedPrimitiveUP*/*argumentCount=9;return resource->DrawIndexedPrimitiveUP(static_cast<D3DPRIMITIVETYPE>(args[1]),static_cast<UINT>(args[2]),static_cast<UINT>(args[3]),static_cast<UINT>(args[4]),static_cast<const void*>(pointer(args[5])),static_cast<D3DFORMAT>(args[6]),static_cast<const void*>(pointer(args[7])),static_cast<UINT>(args[8]));}
case 74:{/*ProcessVertices*/*argumentCount=6;return resource->ProcessVertices(static_cast<UINT>(args[1]),static_cast<UINT>(args[2]),static_cast<UINT>(args[3]),nativeObject<IDirect3DVertexBuffer8>(args[4]),static_cast<DWORD>(args[5]));}
case 75:{/*CreateVertexShader*/*argumentCount=5;return resource->CreateVertexShader(static_cast<const DWORD*>(pointer(args[1])),static_cast<const DWORD*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])),static_cast<DWORD>(args[4]));}
case 76:{/*SetVertexShader*/*argumentCount=2;return resource->SetVertexShader(static_cast<DWORD>(args[1]));}
case 77:{/*GetVertexShader*/*argumentCount=2;return resource->GetVertexShader(static_cast<DWORD*>(pointer(args[1])));}
case 78:{/*DeleteVertexShader*/*argumentCount=2;return resource->DeleteVertexShader(static_cast<DWORD>(args[1]));}
case 79:{/*SetVertexShaderConstant*/*argumentCount=4;return resource->SetVertexShaderConstant(static_cast<DWORD>(args[1]),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]));}
case 80:{/*GetVertexShaderConstant*/*argumentCount=4;return resource->GetVertexShaderConstant(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD>(args[3]));}
case 81:{/*GetVertexShaderDeclaration*/*argumentCount=4;return resource->GetVertexShaderDeclaration(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 82:{/*GetVertexShaderFunction*/*argumentCount=4;return resource->GetVertexShaderFunction(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 83:{/*SetStreamSource*/*argumentCount=4;return resource->SetStreamSource(static_cast<UINT>(args[1]),nativeObject<IDirect3DVertexBuffer8>(args[2]),static_cast<UINT>(args[3]));}
case 84:{/*GetStreamSource*/*argumentCount=4;IDirect3DVertexBuffer8* object2=nullptr;HRESULT status=resource->GetStreamSource(static_cast<UINT>(args[1]),args[2]?&object2:nullptr,static_cast<UINT*>(pointer(args[3])));if(args[2])write(args[2],SUCCEEDED(status)?wrap(object2,Kind::VertexBuffer):0);return status;}
case 85:{/*SetIndices*/*argumentCount=3;return resource->SetIndices(nativeObject<IDirect3DIndexBuffer8>(args[1]),static_cast<UINT>(args[2]));}
case 86:{/*GetIndices*/*argumentCount=3;IDirect3DIndexBuffer8* object1=nullptr;HRESULT status=resource->GetIndices(args[1]?&object1:nullptr,static_cast<UINT*>(pointer(args[2])));if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::IndexBuffer):0);return status;}
case 87:{/*CreatePixelShader*/*argumentCount=3;return resource->CreatePixelShader(static_cast<const DWORD*>(pointer(args[1])),static_cast<DWORD*>(pointer(args[2])));}
case 88:{/*SetPixelShader*/*argumentCount=2;return resource->SetPixelShader(static_cast<DWORD>(args[1]));}
case 89:{/*GetPixelShader*/*argumentCount=2;return resource->GetPixelShader(static_cast<DWORD*>(pointer(args[1])));}
case 90:{/*DeletePixelShader*/*argumentCount=2;return resource->DeletePixelShader(static_cast<DWORD>(args[1]));}
case 91:{/*SetPixelShaderConstant*/*argumentCount=4;return resource->SetPixelShaderConstant(static_cast<DWORD>(args[1]),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]));}
case 92:{/*GetPixelShaderConstant*/*argumentCount=4;return resource->GetPixelShaderConstant(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD>(args[3]));}
case 93:{/*GetPixelShaderFunction*/*argumentCount=4;return resource->GetPixelShaderFunction(static_cast<DWORD>(args[1]),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 94:{/*DrawRectPatch*/*argumentCount=4;return resource->DrawRectPatch(static_cast<UINT>(args[1]),static_cast<const float*>(pointer(args[2])),static_cast<const D3DRECTPATCH_INFO*>(pointer(args[3])));}
case 95:{/*DrawTriPatch*/*argumentCount=4;return resource->DrawTriPatch(static_cast<UINT>(args[1]),static_cast<const float*>(pointer(args[2])),static_cast<const D3DTRIPATCH_INFO*>(pointer(args[3])));}
case 96:{/*DeletePatch*/*argumentCount=2;return resource->DeletePatch(static_cast<UINT>(args[1]));}
}}
if(kind==uint32_t(Kind::Texture)){auto resource=static_cast<IDirect3DTexture8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 7:{/*SetPriority*/*argumentCount=2;return resource->SetPriority(static_cast<DWORD>(args[1]));}
case 8:{/*GetPriority*/*argumentCount=1;return resource->GetPriority();}
case 9:{/*PreLoad*/*argumentCount=1;resource->PreLoad();return 0;}
case 10:{/*GetType*/*argumentCount=1;return resource->GetType();}
case 11:{/*SetLOD*/*argumentCount=2;return resource->SetLOD(static_cast<DWORD>(args[1]));}
case 12:{/*GetLOD*/*argumentCount=1;return resource->GetLOD();}
case 13:{/*GetLevelCount*/*argumentCount=1;return resource->GetLevelCount();}
case 14:{/*GetLevelDesc*/*argumentCount=3;return resource->GetLevelDesc(static_cast<UINT>(args[1]),static_cast<D3DSURFACE_DESC*>(pointer(args[2])));}
case 15:{/*GetSurfaceLevel*/*argumentCount=3;IDirect3DSurface8* object2=nullptr;HRESULT status=resource->GetSurfaceLevel(static_cast<UINT>(args[1]),args[2]?&object2:nullptr);if(args[2])write(args[2],SUCCEEDED(status)?wrap(object2,Kind::Surface):0);return status;}
case 18:{/*AddDirtyRect*/*argumentCount=2;return resource->AddDirtyRect(static_cast<const RECT*>(pointer(args[1])));}
}}
if(kind==uint32_t(Kind::Cube)){auto resource=static_cast<IDirect3DCubeTexture8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 7:{/*SetPriority*/*argumentCount=2;return resource->SetPriority(static_cast<DWORD>(args[1]));}
case 8:{/*GetPriority*/*argumentCount=1;return resource->GetPriority();}
case 9:{/*PreLoad*/*argumentCount=1;resource->PreLoad();return 0;}
case 10:{/*GetType*/*argumentCount=1;return resource->GetType();}
case 11:{/*SetLOD*/*argumentCount=2;return resource->SetLOD(static_cast<DWORD>(args[1]));}
case 12:{/*GetLOD*/*argumentCount=1;return resource->GetLOD();}
case 13:{/*GetLevelCount*/*argumentCount=1;return resource->GetLevelCount();}
case 14:{/*GetLevelDesc*/*argumentCount=3;return resource->GetLevelDesc(static_cast<UINT>(args[1]),static_cast<D3DSURFACE_DESC*>(pointer(args[2])));}
case 15:{/*GetCubeMapSurface*/*argumentCount=4;IDirect3DSurface8* object3=nullptr;HRESULT status=resource->GetCubeMapSurface(static_cast<D3DCUBEMAP_FACES>(args[1]),static_cast<UINT>(args[2]),args[3]?&object3:nullptr);if(args[3])write(args[3],SUCCEEDED(status)?wrap(object3,Kind::Surface):0);return status;}
case 18:{/*AddDirtyRect*/*argumentCount=3;return resource->AddDirtyRect(static_cast<D3DCUBEMAP_FACES>(args[1]),static_cast<const RECT*>(pointer(args[2])));}
}}
if(kind==uint32_t(Kind::VolumeTexture)){auto resource=static_cast<IDirect3DVolumeTexture8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 7:{/*SetPriority*/*argumentCount=2;return resource->SetPriority(static_cast<DWORD>(args[1]));}
case 8:{/*GetPriority*/*argumentCount=1;return resource->GetPriority();}
case 9:{/*PreLoad*/*argumentCount=1;resource->PreLoad();return 0;}
case 10:{/*GetType*/*argumentCount=1;return resource->GetType();}
case 11:{/*SetLOD*/*argumentCount=2;return resource->SetLOD(static_cast<DWORD>(args[1]));}
case 12:{/*GetLOD*/*argumentCount=1;return resource->GetLOD();}
case 13:{/*GetLevelCount*/*argumentCount=1;return resource->GetLevelCount();}
case 14:{/*GetLevelDesc*/*argumentCount=3;return resource->GetLevelDesc(static_cast<UINT>(args[1]),static_cast<D3DVOLUME_DESC*>(pointer(args[2])));}
case 15:{/*GetVolumeLevel*/*argumentCount=3;IDirect3DVolume8* object2=nullptr;HRESULT status=resource->GetVolumeLevel(static_cast<UINT>(args[1]),args[2]?&object2:nullptr);if(args[2])write(args[2],SUCCEEDED(status)?wrap(object2,Kind::Volume):0);return status;}
case 18:{/*AddDirtyBox*/*argumentCount=2;return resource->AddDirtyBox(static_cast<const D3DBOX*>(pointer(args[1])));}
}}
if(kind==uint32_t(Kind::Surface)){auto resource=static_cast<IDirect3DSurface8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 8:{/*GetDesc*/*argumentCount=2;return resource->GetDesc(static_cast<D3DSURFACE_DESC*>(pointer(args[1])));}
}}
if(kind==uint32_t(Kind::Volume)){auto resource=static_cast<IDirect3DVolume8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 8:{/*GetDesc*/*argumentCount=2;return resource->GetDesc(static_cast<D3DVOLUME_DESC*>(pointer(args[1])));}
}}
if(kind==uint32_t(Kind::VertexBuffer)){auto resource=static_cast<IDirect3DVertexBuffer8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 7:{/*SetPriority*/*argumentCount=2;return resource->SetPriority(static_cast<DWORD>(args[1]));}
case 8:{/*GetPriority*/*argumentCount=1;return resource->GetPriority();}
case 9:{/*PreLoad*/*argumentCount=1;resource->PreLoad();return 0;}
case 10:{/*GetType*/*argumentCount=1;return resource->GetType();}
case 13:{/*GetDesc*/*argumentCount=2;return resource->GetDesc(static_cast<D3DVERTEXBUFFER_DESC*>(pointer(args[1])));}
}}
if(kind==uint32_t(Kind::IndexBuffer)){auto resource=static_cast<IDirect3DIndexBuffer8*>(object);switch(method){
case 3:{/*GetDevice*/*argumentCount=2;IDirect3DDevice8* object1=nullptr;HRESULT status=resource->GetDevice(args[1]?&object1:nullptr);if(args[1])write(args[1],SUCCEEDED(status)?wrap(object1,Kind::Device):0);return status;}
case 4:{/*SetPrivateData*/*argumentCount=5;return resource->SetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<const void*>(pointer(args[2])),static_cast<DWORD>(args[3]),static_cast<DWORD>(args[4]));}
case 5:{/*GetPrivateData*/*argumentCount=4;return resource->GetPrivateData(*static_cast<const GUID*>(pointer(args[1])),static_cast<void*>(pointer(args[2])),static_cast<DWORD*>(pointer(args[3])));}
case 6:{/*FreePrivateData*/*argumentCount=2;return resource->FreePrivateData(*static_cast<const GUID*>(pointer(args[1])));}
case 7:{/*SetPriority*/*argumentCount=2;return resource->SetPriority(static_cast<DWORD>(args[1]));}
case 8:{/*GetPriority*/*argumentCount=1;return resource->GetPriority();}
case 9:{/*PreLoad*/*argumentCount=1;resource->PreLoad();return 0;}
case 10:{/*GetType*/*argumentCount=1;return resource->GetType();}
case 13:{/*GetDesc*/*argumentCount=2;return resource->GetDesc(static_cast<D3DINDEXBUFFER_DESC*>(pointer(args[1])));}
}}
