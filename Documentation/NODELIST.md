Event BeginPlay	Runs once when the Actor enters play.
Event Tick	Runs every frame. Use sparingly.
Custom Event	Creates your own executable event.
Branch	An if statement; chooses True or False.
Sequence	Fires several execution paths in order.
Do Once	Allows execution once until reset.
Do N	Allows execution a specified number of times.
Gate	Allows or blocks execution flow.
FlipFlop	Alternates between A and B each time it runs.
Delay	Waits for a specified amount of time.
Set Timer by Event	Runs an event after a delay or repeatedly.
For Loop	Repeats logic a fixed number of times.
For Each Loop	Runs logic once for every item in an array.
While Loop	Repeats while a condition remains true.
Break	Exits certain loops early.
AND	True only if both conditions are true.
OR	True if either condition is true.
NOT	Reverses a Boolean value.
Equal (==)	Checks whether two values are equal.
Not Equal (!=)	Checks whether two values differ.
Greater / Less Than	Compares numeric values.
Select	Chooses a value based on a condition/index.
Switch on Int	Chooses execution based on an integer.
Switch on Enum	Chooses execution based on an Enum value.
Get Variable	Reads the current value of a variable.
Set Variable	Changes the value of a variable.
Increment Int	Adds 1 to an integer.
Decrement Int	Subtracts 1 from an integer.
Clamp	Restricts a number between minimum and maximum values.
Map Range Clamped	Converts a value from one numeric range to another.
Lerp	Interpolates between two values.
Random Float / Int	Generates a random number.
Make Vector	Creates a Vector from X, Y and Z values.
Break Vector	Separates a Vector into X, Y and Z.
Vector + / - / *	Basic Vector mathematics.
Normalize	Turns a Vector into a direction with length 1.
Vector Length	Returns the magnitude of a Vector.
Distance	Finds the distance between two points.
Dot Product	Helps determine directional similarity/alignment.
Make Rotator	Creates a rotation from Pitch, Yaw and Roll.
Find Look at Rotation	Calculates the rotation needed to face a target.
Get Actor Location	Gets an Actor's world position.
Set Actor Location	Moves an Actor to a world position.
Get Actor Rotation	Gets an Actor's world rotation.
Set Actor Rotation	Changes an Actor's rotation.
Get Actor Transform	Gets location, rotation and scale together.
Set Actor Transform	Sets location, rotation and scale together.
Add Actor World Offset	Moves an Actor relative to its current location.
Add Actor Local Offset	Moves an Actor using its local axes.
Add Actor World Rotation	Adds rotation in world space.
Get Forward Vector	Gets the direction an Actor/object faces.
Get Right Vector	Gets its right-facing direction.
Spawn Actor from Class	Creates an Actor during gameplay.
Destroy Actor	Removes an Actor from the world.
Get Actor of Class	Finds one Actor of a specified class.
Get All Actors of Class	Finds all Actors of a specified class.
Get Player Character	Gets the player's Character reference.
Get Player Controller	Gets the player's Player Controller.
Cast To…	Checks/converts an object reference to a specific class type.
Is Valid	Checks whether an object reference is usable.
Get Component by Class	Finds a component attached to an Actor.
Attach Actor to Component	Attaches an Actor to another component/socket.
Detach from Actor	Removes an attachment relationship.
Set Visibility	Shows or hides a component.
Set Hidden in Game	Hides an Actor/component during gameplay.
Set Actor Enable Collision	Enables or disables Actor collision.
On Component Begin Overlap	Fires when something enters a collision/trigger volume.
On Component End Overlap	Fires when something leaves a collision/trigger volume.
Event Hit	Fires when a blocking collision occurs.
Line Trace by Channel	Shoots an invisible line and reports what it hits.
Sphere Trace by Channel	Similar to a Line Trace but with thickness.
Break Hit Result	Extracts hit Actor, location, normal, etc.
Apply Damage	Sends generic damage to an Actor.
Event AnyDamage	Responds when an Actor receives damage.
Play Sound at Location	Plays a sound at a world position.
Spawn Sound Attached	Creates sound attached to an object/component.
Play Animation	Plays an animation on a compatible component.
Play Montage	Plays an Animation Montage on a Character.
Create Widget	Creates a UI Widget instance.
Add to Viewport	Displays a Widget on screen.
Remove from Parent	Removes a Widget from the UI.
Set Text	Changes text displayed by a UI element.
Print String	Displays debug text; extremely useful while developing.
Format Text	Builds formatted text from multiple values.
To String	Converts values into Strings for display/debugging.
Add (Array)	Adds an element to an array.
Remove (Array)	Removes an array element.
Get (Array)	Retrieves an element by index.
Length (Array)	Returns the number of elements.
Contains	Checks whether an array contains something.
Find	Finds an item's index in an array.
Clear	Removes every element from a collection.
Add Unique	Adds an item only if it isn't already present.
Get Data Table Row	Retrieves data from a Data Table.
Set Members in Struct	Changes selected values inside a Struct.
Break Struct	Exposes the individual values stored in a Struct.
Make Struct	Creates a Struct value.
Bind Event to Dispatcher	Connects an event to an Event Dispatcher.
Call Event Dispatcher	Broadcasts an Event Dispatcher to listeners.
Blueprint Interface Message	Calls interface functionality without requiring a specific class.
Get Game Mode	Gets the current Game Mode.
Get Game State	Gets the current Game State.
Get Game Instance	Gets the persistent Game Instance.
Open Level	Loads another level/map.
Get Current Level Name	Returns the currently loaded level's name.
Set Game Paused	Pauses or unpauses gameplay.
Timeline	Animates values over time using curves.
FInterp To	Smoothly interpolates a float toward a target.
VInterp To	Smoothly interpolates a Vector toward a target.
RInterp To	Smoothly interpolates a rotation toward a target.