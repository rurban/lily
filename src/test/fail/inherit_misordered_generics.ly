#[
SyntaxError: Function First::new, argument #1 is invalid:
Expected Type: A
Received Type: B
Where: File "test/fail/inherit_misordered_generics.ly" at line 10
]#

class First[A](value: A) {  }

class Second[A, B](v1: A, v2: B) < First(v2) {  }

# The reason this is considered invalid is because it makes the interpreter
# unable to say that Second's A is First's A. This makes generic resolution
# of properties a lot harder, and...nah.
