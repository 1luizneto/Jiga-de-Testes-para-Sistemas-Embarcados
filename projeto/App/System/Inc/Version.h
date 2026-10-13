/***********************************************************************************************************************
 *   @file       Version.h
 *   @addtogroup System
 *   @brief      Versao do firmware no formato do Versionamento Semantico (MAJOR.MINOR.PATCH).
 *   @details    Atualizar junto com Software/versao.py e o CHANGELOG.md a cada release (ver README.md).
 *   @{
 **********************************************************************************************************************/

#ifndef SYSTEM_INC_VERSION_H_
#define SYSTEM_INC_VERSION_H_

/***********************************************************************************************************************
 * DEFINES PUBLICOS
 **********************************************************************************************************************/

/// Incompatibilidade no protocolo PC <-> jiga
#define dFW_VERSION_MAJOR (0U)

/// Funcionalidade nova, compativel com a versao anterior
#define dFW_VERSION_MINOR (3U)

/// Correcao de bug, sem funcionalidade nova
#define dFW_VERSION_PATCH (0U)

/// Versao em texto, para logs e para o comando STATUS
#define dFW_VERSION_STRING "0.3.0"

#endif /* SYSTEM_INC_VERSION_H_ */

/** @} DOXYGEN GROUP TAG END OF FILE */
