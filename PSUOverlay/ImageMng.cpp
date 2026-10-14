#include "ImageMng.h"
#include "../PSUStructs/Item.h"
#include "../PSUVR/D3D9ExUpgrade.h"

ImageManager::ImageManager()
{
	m_pDevice = nullptr;
	m_whiteTexture = nullptr;
	m_textures.clear();
}

ImageManager::~ImageManager()
{
}

void ImageManager::Initialize( LPDIRECT3DDEVICE9 pDevice )
{
	m_pDevice = pDevice;

	CreateWhiteTexture();

	for( int i = 101; i <= 194; ++i )  // Adjust this range to match your resources
	{
		LoadTextureFromResource( i );
	}
}

void ImageManager::CreateWhiteTexture()
{
	m_whiteTexture = std::make_shared< ImageData >();

	DWORD usage = 0;
	D3DPOOL pool = D3DPOOL_MANAGED;
	DWORD lockFlags = 0;
	if( PSUVR::IsD3D9ExActive() )
	{
		usage = D3DUSAGE_DYNAMIC;
		pool = D3DPOOL_DEFAULT;
		lockFlags = D3DLOCK_DISCARD;
	}

	if( S_OK == D3DXCreateTexture( m_pDevice, 16, 16, 1, usage, D3DFMT_A8R8G8B8, pool, &m_whiteTexture->m_data ) )
	{
		D3DLOCKED_RECT rect;
		auto hr = m_whiteTexture->m_data->LockRect( 0, &rect, nullptr, lockFlags );
		if( FAILED( hr ) )
		{
			MessageBoxA( nullptr, "Failed to lock texture rect.", "Error", MB_OK | MB_ICONERROR );
			return;
		}

		DWORD *pBits = static_cast< DWORD * >( rect.pBits );
		for( int y = 0; y < 16; ++y )
		{
			for( int x = 0; x < 16; ++x )
			{
				pBits[ y * ( rect.Pitch / 4 ) + x ] = D3DCOLOR_XRGB( 255, 255, 255 ); // Fill with white
			}
		}

		m_whiteTexture->m_data->UnlockRect( 0 );

		m_whiteTexture->m_texId = ( ImTextureID )( intptr_t )m_whiteTexture->m_data;
		m_whiteTexture->m_size.x = 16;
		m_whiteTexture->m_size.y = 16;
	}
}

sptr_image ImageManager::LoadTextureFromResource( int resourceId ) const
{
	if( m_textures.count( resourceId ) )
		return m_textures[ resourceId ];  // Return already-loaded texture

	sptr_image loadedImage = LoadTextureFromMemory( resourceId );

	if( loadedImage )
	{
		m_textures[ resourceId ] = loadedImage;
		return loadedImage;
	}

	printf( "Failed to load texture for index: %d\n", resourceId );
	return m_whiteTexture;
}

sptr_image ImageManager::LoadTextureFromMemory( int resourceId ) const
{
    // Obtain the module handle of the DLL
    HMODULE hModule = nullptr;
    if( !GetModuleHandleExW( GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                             ( LPCWSTR )( void * )&ImageManager::Get, &hModule ) )
    {
        printf( "Failed to get DLL module handle\n" );
        return m_whiteTexture;
    }

    // Attempt to locate the resource by index
    HRSRC hResource = FindResource( hModule, MAKEINTRESOURCE( resourceId ), RT_RCDATA );
    if( !hResource )
    {
        printf( "Failed to find resource index: %d\n", resourceId );
        return m_whiteTexture;
    }

    // Load the resource into memory
    HGLOBAL hLoadedResource = LoadResource( hModule, hResource );
    if( !hLoadedResource )
    {
        printf( "Failed to load resource index: %d\n", resourceId );
        return m_whiteTexture;
    }

    // Lock the resource to get a pointer to its data
    void *pResourceData = LockResource( hLoadedResource );
    DWORD resourceSize = SizeofResource( hModule, hResource );

    if( !pResourceData || resourceSize == 0 )
    {
        printf( "Invalid resource data for index: %d\n", resourceId );
        return m_whiteTexture;
    }

    // Create a texture from the memory data with custom settings.
    // D3D9Ex rejects D3DPOOL_MANAGED — use DEFAULT+DYNAMIC on Ex devices.
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_MANAGED;
    if( PSUVR::IsD3D9ExActive() )
    {
        usage = D3DUSAGE_DYNAMIC;
        pool = D3DPOOL_DEFAULT;
    }

    PDIRECT3DTEXTURE9 texture = nullptr;
    HRESULT hr = D3DXCreateTextureFromFileInMemoryEx(
        m_pDevice,
        pResourceData,
        resourceSize,
        D3DX_DEFAULT,          // Width - use actual image width
        D3DX_DEFAULT,          // Height - use actual image height
        D3DX_DEFAULT,          // MipLevels
        usage,
        D3DFMT_A8R8G8B8,       // Desired format
        pool,
        D3DX_DEFAULT,          // Filter
        D3DX_DEFAULT,          // Mip filter
        0,                     // Color key (0 = no colorkey)
        nullptr,               // Image info (optional)
        nullptr,               // Palette (optional)
        &texture               // Output texture
    );

    if( FAILED( hr ) )
    {
        printf( "Failed to create texture from memory for index: %d\n", resourceId );
        return m_whiteTexture;
    }

    // Retrieve description of the texture surface
    D3DSURFACE_DESC my_image_desc;
    texture->GetLevelDesc( 0, &my_image_desc );

    auto image = std::make_shared<ImageData>();

    image->m_texId = ( ImTextureID )( intptr_t )texture;
    image->m_size.x = ( int )my_image_desc.Width;
    image->m_size.y = ( int )my_image_desc.Height;

    return image;
}