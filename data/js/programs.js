// programs.js - Handles program management for programs.html

window.DEBUG = false;

document.addEventListener('DOMContentLoaded', () => {
    loadNavigation(); // Provided by nav.js
    themeManager.initialize(); // Provided by theme.js
    setupProgramUI();
});

function setupProgramUI() {
    loadPrograms();
    // Populate program dropdown and set up event listeners
    document.getElementById('importProgramBtn').onclick = () => {
        console.log('Import button clicked');
        openImportProgramModal();
    };
    document.getElementById('exportProgramBtn').onclick = () => {
        console.log('Export button clicked');
        exportPrograms();
    };
    document.getElementById('startNextBtn').onclick = () => {
        console.log('Start at Next Point button clicked');
        startProgramAtNextPoint();
    };
    document.getElementById('scheduleBtn').onclick = () => {
        console.log('Schedule button clicked');
        openScheduleModal();
    };
    // Modal logic
    const saveModal = document.getElementById('saveProgramModal');
    const closeBtn = document.getElementById('closeSaveProgramModal');
    const cancelBtn = document.getElementById('cancelSaveProgramBtn');
    closeBtn.onclick = cancelBtn.onclick = () => { 
        console.log('Save modal close/cancel clicked');
        saveModal.style.display = 'none'; 
    };
    document.getElementById('confirmSaveProgramBtn').onclick = () => {
        console.log('Confirm save program button clicked');
        submitSaveProgramModal();
    };
}

// Populate dropdown and update preview
function loadPrograms() {
    fetch('/api/programs')
        .then(res => res.json())
        .then(data => {
            const programs = data.programs || [];
            const select = document.getElementById('programSelect');
            select.innerHTML = '';
            programs.forEach((prog, i) => {
                const option = document.createElement('option');
                option.value = prog.id; // Use actual program ID, not array index
                option.textContent = prog.name;
                select.appendChild(option);
            });
            select.onchange = () => displayProgramPreview(programs, select.selectedIndex);
            // Show preview for first program by default
            displayProgramPreview(programs, select.selectedIndex || 0);
        })
        .catch(err => {
            document.getElementById('program-preview').innerHTML = '<p class="error">Failed to load programs.</p>';
        });
}

// Update preview to accept index
function displayProgramPreview(programs, index = 0) {
    if (!programs.length) {
        document.getElementById('program-preview').innerHTML = '<p>No programs saved yet.</p>';
        return;
    }
    const prog = programs[index] || programs[0];
    let temps = prog.temperatures || prog.temps || [];
    // Pad temps to 96 points (or the expected length) with last value or 0
    const expectedPoints = 96; // Could be dynamic if you support variable resolution
    if (temps.length < expectedPoints) {
        const padValue = temps.length > 0 ? temps[temps.length - 1] : 0;
        temps = temps.concat(Array(expectedPoints - temps.length).fill(padValue));
    } else if (temps.length > expectedPoints) {
        temps = temps.slice(0, expectedPoints);
    }
    // --- Trim leading and trailing zeros for preview ---
    let firstNonZero = 0;
    while (firstNonZero < temps.length && temps[firstNonZero] === 0) {
        firstNonZero++;
    }
    let lastNonZero = temps.length - 1;
    while (lastNonZero >= 0 && temps[lastNonZero] === 0) {
        lastNonZero--;
    }
    let trimmedTemps = [];
    let startIndex = 0;
    if (firstNonZero <= lastNonZero) {
        trimmedTemps = temps.slice(firstNonZero, lastNonZero + 1);
        startIndex = firstNonZero;
    }
    // --- Relative timescale labels ---
    const pointsPerDay = window.maxTempPoints || expectedPoints;
    const interval = 1440 / pointsPerDay; // minutes per point
    // Calculate start time in minutes
    const startMinutes = startIndex * interval;
    const startHour = Math.floor(startMinutes / 60);
    const startMinute = Math.round(startMinutes % 60);
    // Generate relative time labels
    const timeLabels = trimmedTemps.map((_, i) => {
        const totalMinutes = i * interval;
        const hours = Math.floor(totalMinutes / 60);
        const minutes = Math.round(totalMinutes % 60);
        return `${hours}h${minutes > 0 ? ":" + minutes.toString().padStart(2, '0') : ''}`;
    });
    // Remove the start time display above the chart
    document.getElementById('program-preview').innerHTML = `<canvas id="programPreviewChart" height="180"></canvas>`;
    const ctx = document.getElementById('programPreviewChart').getContext('2d');
    new Chart(ctx, {
        type: 'line',
        data: {
            labels: timeLabels,
            datasets: [{
                label: prog.name,
                data: trimmedTemps,
                borderColor: '#4CAF50',
                backgroundColor: 'rgba(76,175,80,0.1)',
                fill: true,
                tension: 0.2
            }]
        },
        options: {
            responsive: true,
            plugins: {
                legend: { display: true },
                title: { display: true, text: `Preview: ${prog.name}` }
            },
            scales: {
                x: { title: { display: true, text: 'Time (relative to start)' } },
                y: { title: { display: true, text: 'Temperature (C)' } }
            }
        }
    });
}

function showWarning(message) {
    const warn = document.getElementById('program-warning');
    warn.textContent = message;
    warn.style.display = '';
}

function hideWarning() {
    document.getElementById('program-warning').style.display = 'none';
}

function updateProgramSlotSelect() {
    fetch('/api/programs')
        .then(res => res.json())
        .then(data => {
            const programs = data.programs || [];
            const select = document.getElementById('programSlotSelect');
            select.innerHTML = '';
            let firstEmpty = 0;
            for (let i = 0; i < 10; i++) {
                const prog = programs.find(p => p.id === i || p.index === i);
                const option = document.createElement('option');
                option.value = i;
                if (prog && prog.name) {
                    option.textContent = `Slot ${i+1}: ${prog.name}`;
                } else {
                    option.textContent = `Slot ${i+1} (empty)`;
                    if (firstEmpty === 0) firstEmpty = i;
                }
                select.appendChild(option);
            }
            select.value = firstEmpty;
        })
        .catch(() => {
            // fallback: show all slots
            const select = document.getElementById('programSlotSelect');
            select.innerHTML = '';
            for (let i = 0; i < 10; i++) {
                const option = document.createElement('option');
                option.value = i;
                option.textContent = `Slot ${i+1}`;
                select.appendChild(option);
            }
            select.value = 0;
        });
}

// Call updateProgramSlotSelect when opening the save modal
function openSaveProgramModal() {
    document.getElementById('programNameInput').value = '';
    document.getElementById('programDescInput').value = '';
    updateProgramSlotSelect();
    document.getElementById('saveProgramModal').style.display = 'block';
    document.getElementById('programNameInput').focus();
}

// Save only changed data
function submitSaveProgramModal() {
    const name = document.getElementById('programNameInput').value.trim();
    const description = document.getElementById('programDescInput').value.trim();
    const slotIndex = parseInt(document.getElementById('programSlotSelect').value, 10);
    if (!name) {
        showNotification('Please enter a program name.', 'error');
        return;
    }
    if (isNaN(slotIndex) || slotIndex < 0 || slotIndex > 9) {
        showNotification('Invalid slot number.', 'error');
        return;
    }
    let temps = [];
    if (window.tempChart && window.tempChart.data && window.tempChart.data.datasets[0]) {
        temps = window.tempChart.data.datasets[0].data.map(pt => pt.y || 0);
    } else {
        temps = Array(96).fill(0); // fallback
    }
    // Trim to a single leading zero and remove trailing zeros
    let firstNonZero = 0;
    while (firstNonZero < temps.length && temps[firstNonZero] === 0) {
        firstNonZero++;
    }
    // If there were leading zeros, keep one
    let startIdx = firstNonZero > 0 ? firstNonZero - 1 : 0;
    let lastNonZero = temps.length - 1;
    while (lastNonZero >= 0 && temps[lastNonZero] === 0) {
        lastNonZero--;
    }
    let trimmedTemps = [];
    let startIndex = 0;
    if (startIdx <= lastNonZero) {
        trimmedTemps = temps.slice(startIdx, lastNonZero + 1);
        startIndex = startIdx;
    }
    // After trimming, ensure the first value is 0
    if (trimmedTemps.length > 0 && trimmedTemps[0] !== 0) {
        trimmedTemps.unshift(0);
        startIndex = 0;
    } else if (trimmedTemps.length === 0) {
        trimmedTemps = [0];
        startIndex = 0;
    }
    window.lastProgramStartIndex = startIndex;
    
    const programData = {
        index: slotIndex,
        name: name,
        description: description,
        temps: trimmedTemps
    };
    fetch('/api/saveProgram', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(programData)
    })
    .then(res => res.json())
    .then(data => {
        if (data.success) {
            showNotification('Program saved successfully!', 'success');
            document.getElementById('saveProgramModal').style.display = 'none';
            loadPrograms();
        } else {
            showNotification('Failed to save program: ' + (data.error || 'Unknown error'), 'error');
        }
    })
    .catch(err => {
        showNotification('Failed to save program: ' + err, 'error');
    });
}

function openLoadProgramModal() {
    const slot = prompt('Enter slot number to load (1-10):');
    const slotIndex = parseInt(slot, 10) - 1;
    if (isNaN(slotIndex) || slotIndex < 0 || slotIndex > 9) {
        showNotification('Invalid slot number.', 'error');
        return;
    }
    // Scheduling UI
    const scheduleType = confirm('Click OK to start now, or Cancel to schedule a start time.');
    let scheduleTime = null;
    if (!scheduleType) {
        scheduleTime = prompt('Enter start time (HH:MM, 24h):');
        if (!/^\d{2}:\d{2}$/.test(scheduleTime)) {
            showNotification('Invalid time format. Use HH:MM.', 'error');
            return;
        }
    }
    // Calculate offset (snapping to next available point)
    let offset = 0;
    if (scheduleTime) {
        const [h, m] = scheduleTime.split(':').map(Number);
        // Get system resolution instead of hardcoding 15 minutes
        fetch('/api/status')
            .then(res => res.json())
            .then(status => {
                const pointsPerDay = status.maxTempPoints || 96;
                const interval = 1440 / pointsPerDay; // minutes per point
                const minutes = h * 60 + m;
                offset = Math.floor(minutes / interval);
                // Snap to next available point if not exact
                if (minutes % interval !== 0) offset++;
                if (offset >= pointsPerDay) offset = 0;
                
                // Show warning if not starting at 0
                if (offset !== 0) {
                    showWarning('Warning: Program will start mid-way through.');
                } else {
                    hideWarning();
                }
                
                // Send to backend
                fetch(`/api/loadProgram?id=${slotIndex}&offset=${offset}`)
                    .then(res => res.json())
                    .then(data => {
                        if (data.success) {
                            showNotification('Program loaded successfully!', 'success');
                            loadPrograms();
                        } else {
                            showNotification('Failed to load program: ' + (data.error || 'Unknown error'), 'error');
                        }
                    })
                    .catch(err => {
                        showNotification('Failed to load program: ' + err, 'error');
                    });
            })
            .catch(err => {
                showNotification('Failed to get system status: ' + err, 'error');
            });
        return; // Exit early since we're handling the request asynchronously
    }
    
    // If no schedule time, start immediately (offset = 0)
    // Show warning if not starting at 0
    if (offset !== 0) {
        showWarning('Warning: Program will start mid-way through.');
    } else {
        hideWarning();
    }
    
    // If no schedule time, start immediately (offset = 0)
    // Send to backend
    fetch(`/api/loadProgram?id=${slotIndex}&offset=${offset}`)
        .then(res => res.json())
        .then(data => {
            if (data.success) {
                showNotification('Program loaded successfully!', 'success');
                loadPrograms();
            } else {
                showNotification('Failed to load program: ' + (data.error || 'Unknown error'), 'error');
            }
        })
        .catch(err => {
            showNotification('Failed to load program: ' + err, 'error');
        });
}

function exportPrograms() {
    fetch('/api/programs')
        .then(res => res.json())
        .then(data => {
            const blob = new Blob([JSON.stringify(data.programs || [], null, 2)], {type: 'application/json'});
            const url = URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'programs_export.json';
            document.body.appendChild(a);
            a.click();
            document.body.removeChild(a);
            URL.revokeObjectURL(url);
            showNotification('Programs exported as JSON.', 'success');
        })
        .catch(err => {
            showNotification('Failed to export programs: ' + err, 'error');
        });
}

function openImportProgramModal() {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = 'application/json';
    input.onchange = (e) => {
        const file = e.target.files[0];
        if (!file) return;
        const reader = new FileReader();
        reader.onload = (event) => {
            try {
                const programs = JSON.parse(event.target.result);
                if (!Array.isArray(programs)) throw new Error('Invalid format');
                let successCount = 0, failCount = 0;
                const saveNext = (i) => {
                    if (i >= programs.length) {
                        showNotification(`Import complete. Success: ${successCount}, Failed: ${failCount}`, 'success');
                        loadPrograms();
                        return;
                    }
                    const prog = programs[i];
                    fetch('/api/saveProgram', {
                        method: 'POST',
                        headers: {'Content-Type': 'application/json'},
                        body: JSON.stringify({
                            index: prog.id ?? i,
                            name: prog.name,
                            description: prog.description || '',
                            temps: prog.temperatures || []
                        })
                    })
                    .then(res => res.json())
                    .then(data => {
                        if (data.success) successCount++; else failCount++;
                        saveNext(i+1);
                    })
                    .catch(() => { failCount++; saveNext(i+1); });
                };
                saveNext(0);
            } catch (err) {
                showNotification('Failed to import: ' + err, 'error');
            }
        };
        reader.readAsText(file);
    };
    input.click();
}

function startProgramAtNextPoint() {
    const select = document.getElementById('programSelect');
    const selectedIndex = select.selectedIndex;
    // Get the actual program id/index from the option value (not just the dropdown index)
    const programId = parseInt(select.options[selectedIndex].value, 10);
    
    console.log('Starting program - selectedIndex:', selectedIndex, 'programId:', programId);
    
    // Fetch current resolution/points per day and system time
    fetch('/api/status')
        .then(res => res.json())
        .then(status => {
            const pointsPerDay = status.maxTempPoints || 96;
            
            // Parse system time instead of using browser time
            let systemMinutes = 0;
            if (status.currentTime) {
                // Parse the system time format (HH:MM:SS or HH:MM:SS (M))
                const timeStr = status.currentTime;
                const timeMatch = timeStr.match(/(\d{2}):(\d{2}):(\d{2})/);
                if (timeMatch) {
                    const hours = parseInt(timeMatch[1], 10);
                    const minutes = parseInt(timeMatch[2], 10);
                    systemMinutes = hours * 60 + minutes;
                }
            } else {
                // Fallback to browser time if system time not available
                const now = new Date();
                systemMinutes = now.getHours() * 60 + now.getMinutes();
            }
            
            let offset = status.currentTempIndex;
            if (offset >= pointsPerDay) offset = 0;
            
            console.log('Calculated offset:', offset, 'for system time:', status.currentTime || 'unknown');
            console.log('Backend current temp index:', status.currentTempIndex);
            console.log('System minutes:', systemMinutes, 'Hours:', Math.floor(systemMinutes / 60), 'Minutes:', systemMinutes % 60);
            
            fetch(`/api/loadProgram?id=${programId}&offset=${offset}`)
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        showNotification('Program started at next point!', 'success');
                    } else {
                        showNotification('Failed to start program: ' + (data.error || 'Unknown error'), 'error');
                    }
                })
                .catch(err => {
                    showNotification('Failed to start program: ' + err, 'error');
                });
        })
        .catch(err => {
            showNotification('Failed to get system status: ' + err, 'error');
        });
}

function openScheduleModal() {
    console.log('Opening schedule modal');
    const modal = document.getElementById('scheduleProgramModal');
    const closeBtn = document.getElementById('closeScheduleProgramModal');
    const cancelBtn = document.getElementById('cancelScheduleBtn');
    const confirmBtn = document.getElementById('confirmScheduleBtn');
    const timeInput = document.getElementById('scheduleTimeInput');
    const warningDiv = document.getElementById('scheduleWarning');
    
    if (!modal || !closeBtn || !cancelBtn || !confirmBtn || !timeInput || !warningDiv) {
        console.error('Schedule modal elements not found:', {
            modal: !!modal,
            closeBtn: !!closeBtn,
            cancelBtn: !!cancelBtn,
            confirmBtn: !!confirmBtn,
            timeInput: !!timeInput,
            warningDiv: !!warningDiv
        });
        return;
    }
    
    // Reset modal state
    timeInput.value = '';
    warningDiv.style.display = 'none';
    warningDiv.textContent = '';
    modal.style.display = 'block';

    function closeModal() {
        console.log('Closing schedule modal');
        modal.style.display = 'none';
        warningDiv.textContent = '';
        warningDiv.style.display = 'none';
        // Always reset the handler to the original
        confirmBtn.onclick = originalConfirmHandler;
    }
    
    // Set up close and cancel button handlers
    closeBtn.onclick = () => {
        console.log('Schedule modal close button clicked');
        closeModal();
    };
    cancelBtn.onclick = () => {
        console.log('Schedule modal cancel button clicked');
        closeModal();
    };
    
    // Save the original handler so we can always reset
    const originalConfirmHandler = function() {
        console.log('Schedule confirm button clicked');
        const timeVal = timeInput.value;
        console.log('Time value:', timeVal);
        if (!/^\d{2}:\d{2}$/.test(timeVal)) {
            console.log('Invalid time format');
            warningDiv.textContent = 'Please enter a valid time in HH:MM format.';
            warningDiv.style.display = '';
            return;
        }
        // Ensure select and selectedIndex are defined before use
        const select = document.getElementById('programSelect');
        const selectedIndex = select.selectedIndex;
        // Calculate offset
        const [h, m] = timeVal.split(':').map(Number);
        fetch('/api/status')
            .then(res => res.json())
            .then(status => {
                const pointsPerDay = status.maxTempPoints || 96;
                let offset = (calculateOffsetFromTime(h, m, pointsPerDay) - 1 + pointsPerDay) % pointsPerDay;
                
                if (selectedIndex === -1 || select.options.length === 0) {
                    console.error('No program selected or no programs available');
                    showNotification('Please select a program first', 'error');
                    return;
                }
                
                const programId = parseInt(select.options[selectedIndex].value, 10);
                console.log('Selected index:', selectedIndex, 'Program ID:', programId);
                console.log('Available options:', Array.from(select.options).map(opt => ({value: opt.value, text: opt.textContent})));
                
                if (isNaN(programId)) {
                    console.error('Invalid program ID:', select.options[selectedIndex].value);
                    showNotification('Invalid program selection', 'error');
                    return;
                }
                
                fetch(`/api/programs`)
                  .then(res => res.json())
                  .then(data => {
                    console.log('Programs data:', data);
                    console.log('Looking for program with ID:', programId);
                    const prog = (data.programs || []).find(p => p.id === programId);
                    console.log('Found program:', prog);
                    if (!prog) {
                        console.error('Program not found for ID:', programId);
                        showNotification('Selected program not found', 'error');
                        return;
                    }
                    let temps = prog.temperatures || prog.temps || [];
                    // Trim to a single leading zero and remove trailing zeros
                    let firstNonZero = 0;
                    while (firstNonZero < temps.length && temps[firstNonZero] === 0) firstNonZero++;
                    let startIdx = firstNonZero > 0 ? firstNonZero - 1 : 0;
                    let lastNonZero = temps.length - 1;
                    while (lastNonZero >= 0 && temps[lastNonZero] === 0) lastNonZero--;
                    let trimmedLen = (startIdx <= lastNonZero) ? (lastNonZero - startIdx + 1) : 0;
                    // Calculate current time index (matches backend formula)
                    let now = new Date();
                    const tempResolution = pointsPerDay / 24; // 96 points = 4 per hour
                    let currentIndex = (now.getHours() * tempResolution) + Math.floor(now.getMinutes() * tempResolution / 60);
                    // Program active window: [offset, offset+trimmedLen)
                    let inWindow = false;
                    if (trimmedLen > 0) {
                      if (offset <= currentIndex && currentIndex < offset + trimmedLen) {
                        inWindow = true;
                      } else if (offset + trimmedLen > pointsPerDay && currentIndex < (offset + trimmedLen) % pointsPerDay) {
                        inWindow = true;
                      }
                    }
                    if (inWindow) {
                      warningDiv.textContent = 'Warning: Program will start mid-way through. Continue?';
                      warningDiv.style.display = '';
                      // Only proceed if user confirms again
                      confirmBtn.onclick = function() {
                        startScheduledProgram(offset);
                        closeModal();
                      };
                      return;
                    } else {
                      startScheduledProgram(offset);
                      closeModal();
                    }
                  });
            });
    };
    confirmBtn.onclick = originalConfirmHandler;
}

// Helper function to calculate the correct offset based on time (matches backend formula)
function calculateOffsetFromTime(hours, minutes, pointsPerDay) {
    const tempResolution = pointsPerDay / 24;
    const interval = 60 / tempResolution;
    const pointIndex = (hours * tempResolution) + Math.floor(minutes * tempResolution / 60);
    // If minutes is not exactly on a boundary, round up to next point
    if (minutes % interval !== 0) {
        return pointIndex + 1;
    }
    return pointIndex;
}

// Helper function to calculate the next point offset (for "Start at Next Point")
function calculateNextPointOffset(hours, minutes, pointsPerDay) {
    const tempResolution = pointsPerDay / 24; // 96 points = 4 per hour
    const currentPointIndex = (hours * tempResolution) + Math.floor(minutes * tempResolution / 60);
    const nextPointIndex = currentPointIndex + 1;
    
    console.log(`Next Point - Time: ${hours}:${minutes.toString().padStart(2, '0')}, Points per day: ${pointsPerDay}, Temp resolution: ${tempResolution}, Current point: ${currentPointIndex}, Next point: ${nextPointIndex}`);
    
    return nextPointIndex;
}

// Helper function to wrap offset for circular array
function wrapOffset(offset, pointsPerDay) {
    return (offset - 1 + pointsPerDay) % pointsPerDay;
}

function startScheduledProgram(offset) {
    console.log('startScheduledProgram called with offset:', offset);
    const select = document.getElementById('programSelect');
    const selectedIndex = select.selectedIndex;
    const programId = parseInt(select.options[selectedIndex].value, 10);
    
    console.log('Scheduling program - selectedIndex:', selectedIndex, 'programId:', programId, 'offset:', offset);
    
    fetch(`/api/loadProgram?id=${programId}&offset=${offset}`)
        .then(res => res.json())
        .then(data => {
            if (data.success) {
                showNotification('Program scheduled and started at selected time!', 'success');
            } else {
                showNotification('Failed to start program: ' + (data.error || 'Unknown error'), 'error');
            }
        })
        .catch(err => {
            showNotification('Failed to start program: ' + err, 'error');
        });
} 