# epsilon_add_shader(<fuente> <perfil> <funcion> <variable> <cabecera>)
# Compila un shader a DXIL y genera una cabecera con el bytecode. Un juego la usa para sus
# propios efectos y se los pasa a SdlGpuBackend::AddPostEffect.
# Requiere la variable EPSILON_DXC con la ruta de dxc.
function(epsilon_add_shader source profile entry variable output)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${EPSILON_DXC} -nologo -T ${profile} -E ${entry} -Vn ${variable} -Fh ${output} ${source}
        DEPENDS ${source}
        COMMENT "dxc ${entry}"
        VERBATIM)
endfunction()