"""Builder for encoded value headers

The following rule creates a target "myencodedvalue.h" which generates a specific
associated header from "myencodedvalue.py"

EncodedValue('myencodedvalue.h')

"""

import os
from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Script import Dir, File

encoded_value_action = Action('$PYTHON $SOURCE > $TARGET', 'Generating encoded_value $TARGET')

def encoded_value_emitter(target, source, env):
    env.Depends(target, File('#/src/mongo/util/encoded_value/encoded_value.py'))
    env.Prepend(ENV= { 'PYTHONPATH' : Dir('#/src/mongo/util/encoded_value').abspath })

    processed_sources = []
    processed_targets = []

    for s in source:
        processed_targets.append(s.abspath)
        processed_sources.append(os.path.splitext(s.srcnode().abspath)[0] + '.py')

    return processed_targets, processed_sources

encoded_value_builder = Builder(
    action=encoded_value_action,
    emitter=encoded_value_emitter)

def exists( env ):
    return True

def generate( env ):
    env['BUILDERS']['EncodedValue'] = encoded_value_builder
